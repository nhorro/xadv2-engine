# Workflow for packing the avatar spritesheets. 
# This is a simple script that can be run from the command line to pack the spritesheets 
# for the avatar.

# Input and output directories
IMAGE_INPUT_DIR=data/test_input
IMAGE_OUTPUT_DIR=data/test_output

# Name of the image files to be packed. The script will look for files with this prefix 
# in the input directory. Conventions:
# <something>_body.png for the body spritesheet and <something>_head.png for the head spritesheet.
IMAGE_ID_PREFIX=julia  

# Step 1. Pack the spritesheets for the body and head. The script will look for files with 
# the prefix + "_body.png" and "_head.png" in the input directory, 
# and will output the packed spritesheets and atlas files in the output directory.

# BODY
#ls $IMAGE_INPUT_DIR/${IMAGE_ID_PREFIX}_body.png
python3 spritesheet_packer/pack_spritesheet.py \
     --frames-dir $IMAGE_OUTPUT_DIR/$IMAGE_ID_PREFIX/body_frames \
     --out-image  $IMAGE_OUTPUT_DIR/$IMAGE_ID_PREFIX/body.png \
     --out-yaml $IMAGE_OUTPUT_DIR/$IMAGE_ID_PREFIX/atlas_body.yml \
     --add-anchor "walking_pivot" 0.5 1.0 \
     $IMAGE_INPUT_DIR/${IMAGE_ID_PREFIX}_body.png

# HEAD
python3 spritesheet_packer/pack_spritesheet.py \
     --frames-dir $IMAGE_OUTPUT_DIR/$IMAGE_ID_PREFIX/head_frames \
     --out-image  $IMAGE_OUTPUT_DIR/$IMAGE_ID_PREFIX/head.png \
     --out-yaml $IMAGE_OUTPUT_DIR/$IMAGE_ID_PREFIX/atlas_head.yml \
     --add-anchor "neck_pivot" 0.5 0.1 \
     $IMAGE_INPUT_DIR/${IMAGE_ID_PREFIX}_head.png


# Step 2. Merge the images into a single one. The script will look for the packed spritesheets and atlas files in the output directory,
# and will output a single image and atlas file that contains both the body and head spritesheets
# MERGE
BODY_IMG="$IMAGE_OUTPUT_DIR/$IMAGE_ID_PREFIX/body.png"
BODY_YML="$IMAGE_OUTPUT_DIR/$IMAGE_ID_PREFIX/atlas_body.yml"
HEAD_IMG="$IMAGE_OUTPUT_DIR/$IMAGE_ID_PREFIX/head.png"
HEAD_YML="$IMAGE_OUTPUT_DIR/$IMAGE_ID_PREFIX/atlas_head.yml"
MERGED_IMG="$IMAGE_OUTPUT_DIR/$IMAGE_ID_PREFIX/merged.png"
MERGED_YML="$IMAGE_OUTPUT_DIR/$IMAGE_ID_PREFIX/atlas_merged.yml"

# call the merge script (provided in this directory)
python3 spritesheet_packer/merge_atlases.py \
     --atlas "$BODY_IMG" "$BODY_YML" --namespace body \
     --atlas "$HEAD_IMG" "$HEAD_YML" --namespace head \
     --out-image "$MERGED_IMG" --out-yaml "$MERGED_YML" \
     --max-width 1024 --padding 2

echo "Merged atlas: $MERGED_IMG" 
echo "Merged yaml: $MERGED_YML"