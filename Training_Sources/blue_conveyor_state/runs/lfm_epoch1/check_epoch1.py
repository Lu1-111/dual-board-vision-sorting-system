import json,torch
from pathlib import Path
from PIL import Image
from peft import PeftModel
from transformers import AutoProcessor, Lfm2VlForConditionalGeneration
base='/root/lfm-workcell/models/LFM2.5-VL-450M'
adapter='/root/lfm-workcell/outputs/workcell_epoch1'
cfg=json.loads(Path('/root/lfm-workcell/train_epoch1.json').read_text())
rows=[json.loads(s) for s in Path(cfg['train_manifest']).read_text().splitlines()]
processor=AutoProcessor.from_pretrained(adapter,local_files_only=True)
model=Lfm2VlForConditionalGeneration.from_pretrained(base,dtype=torch.bfloat16,attn_implementation='sdpa',local_files_only=True)
model=PeftModel.from_pretrained(model,adapter).to('cuda').eval()
selected=[]
for label in cfg['classes']:
 selected.extend([r for r in rows if r['label']==label][:3])
correct=0
for r in selected:
 p=Path(cfg['train_manifest']).parent/r['image']
 with Image.open(p) as im: image=im.convert('RGB')
 messages=[{'role':'user','content':[{'type':'image','image':image},{'type':'text','text':cfg['prompt']}]}]
 inputs=processor.apply_chat_template(messages,add_generation_prompt=True,tokenize=True,return_dict=True,return_tensors='pt').to('cuda')
 with torch.inference_mode(): output=model.generate(**inputs,max_new_tokens=12,do_sample=False,use_cache=False)
 answer=processor.batch_decode(output[:,inputs['input_ids'].shape[1]:],skip_special_tokens=True)[0].strip()
 correct += answer==r['label']
 print(json.dumps({'image':r['image'],'label':r['label'],'output':answer},ensure_ascii=False),flush=True)
print('TRAINING_IMAGE_SANITY',correct,'/',len(selected),flush=True)
