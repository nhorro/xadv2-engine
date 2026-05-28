# Build and run the Python authoring tools (issue #125): the room editor,
# avatarmaker, and the sprite / chroma packers.
#
# The room editor and avatarmaker are stdlib-only `http.server` web UIs (open in
# a browser), so they only need Python. The packers use numpy + OpenCV; the
# headless OpenCV wheel is used because the containerized tools are CLI/web (the
# interactive cv2 tuner GUI is best run on the host, or via an X11-enabled run).
FROM python:3.12-slim

# glib is needed at `import cv2` time even for the headless wheel.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libglib2.0-0 \
    && rm -rf /var/lib/apt/lists/*

COPY docker/tools-requirements.txt /tmp/tools-requirements.txt
RUN pip install --no-cache-dir -r /tmp/tools-requirements.txt

WORKDIR /work
COPY . /work
# `tools` resolves as a package because the working dir is on sys.path for `-m`;
# tools/ on PYTHONPATH additionally lets the editors' `room_editor.*` and sibling
# imports resolve.
ENV PYTHONPATH=/work/tools

EXPOSE 8000
# Serve the room editor on all interfaces so the host browser reaches it via the
# published port. Defaults to the sample game's rooms folder.
CMD ["python3", "-m", "tools.room_editor", "serve", \
     "--base-path", "games/themummy/data/rooms", \
     "--host", "0.0.0.0", "--port", "8000"]
