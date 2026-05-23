# Character processing pipeline - Step 1: Transparentize
# This script takes the original character spritesheet and applies a transparentization process to it.
# The transparentization process works as follows: 
# distancia <= hard  → transparente total
# distancia >= soft  → opaco total
# entre ambas        → alfa gradual

PYTHON_TOOL_PATH=../../../../tools/transparentizer

LAYER_FILENAME=01.png
INPUT_DIR=./00_original/
OUTPUT_DIR=./01_transparentization/

# Command to run the transparentizer tool
PYTHONPATH=${PYTHON_TOOL_PATH} python3 . \
        "${INPUT_DIR}/${LAYER_FILENAME}" \
        "${OUTPUT_DIR}/${LAYER_FILENAME}" \
        --key auto \
        --hard 100 \
        --soft 125 \
        --padding 2