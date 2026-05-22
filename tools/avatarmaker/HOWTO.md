Procedure - Generation of avatar files
======================================

Input

- Transparent spritesheet of body: avatar_body.png
- Transparent spritesheet of head: avatar_head.png

Note: change "avatar" for the name of the PC.

1. Edit pack.sh with IMAGE_ID_PREFIX and run.
2. Edit atlas_merged.yml. Replace frame names.
Use the following convention:

- Part
    - b (body)
    - h (head)
- Direction: 
    - u (up)
    - r (right)
    - l (left)
    - d (down)
- Pose: 
    - w (walking)
    - s (standing)
- Use 0,1,2,.. for frame sequence number.

Example: 
    - Body up standing: bus0
    - Body right walking, frame 0: brw0
    - Body right walking, frame 1: brw1
    - Etc.

3. Run the avatar composer (see example .sh). Create one frame for standing in each pose, then save animation.
4. Edit the animation. Copy the frames for the remaining walking sequences and manually select the sprites.
5. Re-run composer and edit position as needed.

