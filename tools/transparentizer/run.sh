# distancia <= hard  → transparente total
# distancia >= soft  → opaco total
# entre ambas        → alfa gradual

# Archivo
#INPUT_FILE=../../games/themummy/data/rooms/a/background/01.png
#python3 . "$INPUT_FILE" output.png --key auto --hard 100 --soft 225 --padding 2

# Directorio


#INPUT_DIR=../../games/themummy/data/rooms/a/background/
#python3 . "$INPUT_DIR" ./clipped/a --key auto --hard 100 --soft 225 --padding 2

#INPUT_DIR=../../games/themummy/data/rooms/b/background/
#python3 . "$INPUT_DIR" ./clipped/b --key auto --hard 100 --soft 225 --padding 2

INPUT_DIR=../../games/themummy/data/rooms/c/background/
python3 . "$INPUT_DIR" ./clipped/c --key auto --hard 100 --soft 125 --padding 2