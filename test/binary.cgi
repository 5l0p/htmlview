#!/usr/bin/env python3
# Demo CGI for htmlview binary mode: dispatches by Content-Type.
import os, sys, io

path = os.environ.get("PATH_INFO", "/")

def emit(ctype, data):
    sys.stdout.write("Content-Type: %s\r\nContent-Length: %d\r\n\r\n" % (ctype, len(data)))
    sys.stdout.flush()
    sys.stdout.buffer.write(data)

if path == "/img":
    from PIL import Image
    im = Image.new("RGB", (140, 90), (40, 120, 200))
    for x in range(140):
        for y in range(90):
            if (x // 14 + y // 15) % 2 == 0:
                im.putpixel((x, y), (240, 200, 40))
    buf = io.BytesIO(); im.save(buf, "PNG")
    emit("image/png", buf.getvalue())
elif path == "/file":
    emit("application/octet-stream", (b"\x00\x01\x02RAW-BYTES\xff\xfe" * 16))
elif path == "/txt":
    emit("text/plain", b"plain text\n  preserved   spacing\nline 3\n")
else:
    print("Content-Type: text/html\r\n")
    print("<title>Binary demo</title><h1>Binary mode demo</h1>")
    print('<p><a href="/img">view a PNG image</a></p>')
    print('<p><a href="/file">an octet-stream (saved + info)</a></p>')
    print('<p><a href="/txt">plain text (rendered as &lt;pre&gt;)</a></p>')
