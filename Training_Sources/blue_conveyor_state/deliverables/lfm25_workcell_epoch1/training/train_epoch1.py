import argparse
import json
import math
import re
from pathlib import Path

import torch
from PIL import Image
from torch.utils.data import Dataset
from transformers import AutoProcessor, Lfm2VlForConditionalGeneration, Trainer, TrainingArguments, set_seed
from peft import LoraConfig, get_peft_model
from trl.trainer.sft_trainer import DataCollatorForVisionLanguageModeling

BASE = Path(__file__).resolve().parent

def load_stack(cfg):
    processor = AutoProcessor.from_pretrained(cfg['model_path'], local_files_only=True)
    processor.image_processor.do_image_splitting = False
    model = Lfm2VlForConditionalGeneration.from_pretrained(
        cfg['model_path'], dtype=torch.bfloat16, attn_implementation='sdpa', local_files_only=True)
    targets = [n for n, layer in model.named_modules()
               if isinstance(layer, torch.nn.Linear) and 'language_model' in n and not n.endswith('lm_head')]
    if not targets:
        raise RuntimeError('No language-model LoRA targets found')
    model = get_peft_model(model, LoraConfig(
        r=cfg['lora_r'], lora_alpha=cfg['lora_alpha'], lora_dropout=cfg['lora_dropout'],
        target_modules='^(?:' + '|'.join(re.escape(n) for n in targets) + ')$',
        bias='none', task_type='CAUSAL_LM'))
    model.config.use_cache = False
    model.print_trainable_parameters()
    trainable = [n for n,p in model.named_parameters() if p.requires_grad]
    assert trainable and all('lora_' in n and 'language_model' in n for n in trainable)
    return model, processor

class Records(Dataset):
    def __init__(self, path, cfg):
        path = Path(path)
        self.rows = [json.loads(s) for s in path.read_text().splitlines() if s.strip()]
        if not self.rows:
            raise ValueError(f'Empty dataset: {path}')
        for r in self.rows:
            if r['label'] not in cfg['classes']:
                raise ValueError(f'Invalid label: {r}')
            image = Path(r['image'])
            r['image'] = str(image if image.is_absolute() else path.parent / image)
            with Image.open(r['image']) as im:
                im.verify()
    def __len__(self): return len(self.rows)
    def __getitem__(self, i): return dict(self.rows[i])

class Collator:
    def __init__(self, processor, cfg):
        self.cfg = cfg
        self.inner = DataCollatorForVisionLanguageModeling(processor, max_length=None, completion_only_loss=True)
    def __call__(self, rows):
        examples = []
        for r in rows:
            if isinstance(r['image'], Image.Image):
                image = r['image'].copy().convert('RGB')
            else:
                with Image.open(r['image']) as im:
                    image = im.convert('RGB')
            examples.append(dict(
                images=[image],
                prompt=[dict(role='user', content=[dict(type='image'), dict(type='text', text=self.cfg['prompt'])])],
                completion=[dict(role='assistant', content=[dict(type='text', text=r['label'])])]))
        batch = self.inner(examples)
        if not (batch['labels'] != -100).any(dim=1).all():
            raise RuntimeError('A sample has no supervised answer tokens')
        return batch

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--config', default=str(BASE/'train_config.json'))
    parser.add_argument('--smoke', action='store_true')
    args=parser.parse_args()
    cfg=json.loads(Path(args.config).read_text())
    set_seed(cfg['seed'])
    if not torch.cuda.is_available() or not torch.cuda.is_bf16_supported():
        raise RuntimeError('A BF16-capable CUDA GPU is required')
    if args.smoke:
        # Artificial inputs verify execution only and are excluded from real training.
        train=[dict(image=Image.new('RGB',(512,288),(40,80,50)), label='empty'),
               dict(image=Image.new('RGB',(512,288),(50,60,90)), label='object_present')]
        val=None
    else:
        train=Records(cfg['train_manifest'],cfg)
        val=Records(cfg['val_manifest'],cfg) if cfg.get('val_manifest') else None
    model, processor=load_stack(cfg)
    collator=Collator(processor,cfg)
    if args.smoke:
        batch=collator(train)
        print('BATCH', {k:list(v.shape) for k,v in batch.items() if torch.is_tensor(v)})
        print('ANSWER_TOKENS', processor.tokenizer.batch_decode([r[r!=-100] for r in batch['labels']]))
    output=str(BASE/'outputs/smoke') if args.smoke else cfg['output_dir']
    training_args=TrainingArguments(
        output_dir=output, num_train_epochs=cfg['epochs'], max_steps=1 if args.smoke else -1,
        per_device_train_batch_size=2 if args.smoke else cfg['batch_size'],
        per_device_eval_batch_size=cfg['batch_size'],
        gradient_accumulation_steps=1 if args.smoke else cfg['gradient_accumulation_steps'],
        learning_rate=cfg['learning_rate'], lr_scheduler_type='cosine',
        warmup_steps=0 if args.smoke else max(1, math.ceil(math.ceil(math.ceil(len(train)/cfg['batch_size'])/cfg['gradient_accumulation_steps'])*cfg['epochs']*cfg['warmup_ratio'])), weight_decay=cfg['weight_decay'],
        bf16=True, gradient_checkpointing=True,
        gradient_checkpointing_kwargs={'use_reentrant':False},
        optim='adamw_torch', max_grad_norm=1.0,
        eval_strategy='epoch' if val is not None else 'no',
        save_strategy='no' if args.smoke else 'epoch',
        load_best_model_at_end=val is not None,
        metric_for_best_model='eval_loss', greater_is_better=False,
        save_total_limit=2, logging_steps=1 if args.smoke else 5,
        remove_unused_columns=False, report_to='none', dataloader_num_workers=4, seed=cfg['seed'])
    trainer=Trainer(model=model,args=training_args,train_dataset=train,eval_dataset=val,
                    data_collator=collator,processing_class=processor)
    result=trainer.train()
    if not math.isfinite(result.training_loss):
        raise RuntimeError('Non-finite training loss')
    trainer.save_model(output)
    processor.save_pretrained(output)
    Path(output,'training_config.json').write_text(json.dumps(cfg,ensure_ascii=False,indent=2))
    print('RESULT',json.dumps(dict(loss=result.training_loss,metrics=result.metrics,
        peak_gpu_gib=torch.cuda.max_memory_allocated()/2**30,smoke_only=args.smoke),ensure_ascii=False))

if __name__=='__main__': main()
