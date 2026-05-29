# Character processing pipeline - Step 3: Pack
# This script takes the transparentized character spritesheet and packs it into a format suitable for use in the game. 
# It uses a tool called "spritesheet_packer" to create a new sprites
# The path of the tool
PYTHON_TOOL_PATH=../../../../../tools/spritesheet_packer/

SPRITESHEET_BASENAME=truck
INPUT_DIR=../02_adjust_size/
INPUT_TXT_DIR=../03_frame_naming/
OUTPUT_DIR=.

#--frames-dir $OUTPUT_DIR/${SPRITESHEET_BASENAME}_frames \
python3 ${PYTHON_TOOL_PATH}/pack_spritesheet.py \
     --out-image  $OUTPUT_DIR/${SPRITESHEET_BASENAME}.png \
     --out-yaml $OUTPUT_DIR/${SPRITESHEET_BASENAME}.yml \
     --names-file $INPUT_TXT_DIR/${SPRITESHEET_BASENAME}.txt \
     --add-anchor "walking_pivot" 0.5 1.0 \
     $INPUT_DIR/${SPRITESHEET_BASENAME}.png