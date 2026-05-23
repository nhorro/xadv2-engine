# Character processing pipeline - Step 3: Pack
# This script takes the transparentized character spritesheet and packs it into a format suitable for use in the game. 
# It uses a tool called "spritesheet_packer" to create a new sprites
# The path of the tool
PYTHON_TOOL_PATH=../../../../tools/spritesheet_packer

SPRITESHEET_BASENAME=julia
INPUT_DIR=./01_transparentization/
INPUT_TXT_DIR=./02_frame_namification/
OUTPUT_DIR=./03_packagification/

python3 ${PYTHON_TOOL_PATH}/pack_spritesheet.py \
     --out-image  $OUTPUT_DIR/${SPRITESHEET_BASENAME}.png \
     --out-yaml $OUTPUT_DIR/${SPRITESHEET_BASENAME}.yml \
     --frames-dir $OUTPUT_DIR/${SPRITESHEET_BASENAME}_frames \
     --names-file $INPUT_TXT_DIR/${SPRITESHEET_BASENAME}.txt \
     --add-anchor "walking_pivot" 0.5 1.0 \
     $INPUT_DIR/${SPRITESHEET_BASENAME}.png