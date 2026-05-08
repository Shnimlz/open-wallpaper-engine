import os
import re

replacements = {
    r'\bIdeaTime\b': 'IdealTime',
    r'\bEmitt\b': 'Emit',
    r'\bparitileSys\b': 'particleSys',
    r'\bAttatchNode\b': 'AttachNode',
    r'\bAttatchImgEffect\b': 'AttachImgEffect',
    r'\bdefualt_mesh\b': 'default_mesh',
    r'\borgin\b': 'origin',
    r'\bParticleEmittOp\b': 'ParticleEmitOp',
    r'\bMakeEmittOp\b': 'MakeEmitOp',
    r'\bm_emiters\b': 'm_emitters'
}

def process_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()
    
    new_content = content
    for pattern, repl in replacements.items():
        new_content = re.sub(pattern, repl, new_content)
        
    if new_content != content:
        with open(filepath, 'w') as f:
            f.write(new_content)
        print(f"Updated {filepath}")

for root, _, files in os.walk('src'):
    for file in files:
        if file.endswith(('.cpp', '.cppm', '.h', '.hpp', '.txt')):
            process_file(os.path.join(root, file))
