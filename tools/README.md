Tools
=====

This directory contains tools to edit game assets. 

Tools currently are minimal, low-quality, to get game assets quickly.

Available tools
---------------

- [Transparentizer](./transparentizer/). ChatGPT and other diffusion models do not generate real transparent images, neither with a background solid color (it is noisy), so an algorithm to remove using threesholds is needed. This tool does this in a rudimentary way, but enough for now.
- [Spritesheet packer](./spritesheet_packer/). ChatGPT and other diffusion models may generate spritesheets, but with artifacts and without respecting a grid. So, this tool takes an unoptimized spritesheet and packs it (produces an optimized size .png + .yaml).
- [Room editor](./room_editor/)


Ugly, questionable tools
------------------------

- [Avatar maker](./avatarmaker/). This was an experimental editor for CompositeSprites. Requires reconsidering its usefulness and a redesign.