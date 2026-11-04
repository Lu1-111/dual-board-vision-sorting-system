import json
from pathlib import Path
import numpy as np,torch,onnxruntime as ort
from PIL import Image
from torchvision import models
import sys
sys.path.insert(0,'/root/lfm-workcell/cnn_compare')
from train_parallel import Workcell,CLASSES
root=Path('/root/lfm-workcell/cnn_compare/run_20261102_parallel')
rows=[{'path':str(Path('/root/lfm-workcell/new_test/images')/n),'label':l} for n,l in [('01_empty.png','empty'),('02_object_present.png','object_present'),('03_hand_present.png','hand_present')]]
ds=Workcell(rows,False)
torch.set_num_threads(1)
for name in ['mobilenet_v3_small','mobilenet_v3_large','efficientnet_b0','resnet18']:
 if name=='mobilenet_v3_small':m=models.mobilenet_v3_small(weights=None);m.classifier[-1]=torch.nn.Linear(m.classifier[-1].in_features,3)
 elif name=='mobilenet_v3_large':m=models.mobilenet_v3_large(weights=None);m.classifier[-1]=torch.nn.Linear(m.classifier[-1].in_features,3)
 elif name=='efficientnet_b0':m=models.efficientnet_b0(weights=None);m.classifier[-1]=torch.nn.Linear(m.classifier[-1].in_features,3)
 else:m=models.resnet18(weights=None);m.fc=torch.nn.Linear(m.fc.in_features,3)
 checkpoint=torch.load(root/name/'model.pt',map_location='cpu',weights_only=True);m.load_state_dict(checkpoint['state_dict']);m.eval()
 sess=ort.InferenceSession(str(root/name/'model_fp32.onnx'),providers=['CPUExecutionProvider'])
 differences=[];preds=[]
 for i in range(len(ds)):
  x,_=ds[i];x=x.unsqueeze(0)
  with torch.inference_mode():reference=m(x).numpy()
  output=sess.run(['logits'],{'images':x.numpy()})[0]
  differences.append(float(np.max(np.abs(reference-output))));preds.append(CLASSES[int(output.argmax())])
 assert max(differences)<0.0002,(name,differences)
 print(json.dumps({'model':name,'max_abs_difference':max(differences),'predictions':preds}),flush=True)
