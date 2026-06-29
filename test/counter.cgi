#!/usr/bin/env python3
# Sample CGI app for htmlview --cgi-bin.
# Re-spawned per request, so state lives in a file between spawns.
import os, urllib.parse

STATE = "/tmp/htmlview_counter.txt"

def load():
    try:
        c, n = open(STATE).read().split("\n", 1)
        return int(c), n
    except Exception:
        return 0, "world"

def save(c, n):
    open(STATE, "w").write("%d\n%s" % (c, n))

path = os.environ.get("PATH_INFO", "/")
q = urllib.parse.parse_qs(os.environ.get("QUERY_STRING", ""))
c, n = load()
if path == "/inc":
    c += 1
if "name" in q:
    n = q["name"][0]
save(c, n)

print("Content-Type: text/html")
print()
print("""<html><head><title>Counter</title>
<style>
  body { font-family: sans-serif; margin: 20px; background: #f4f6fb; }
  h1 { color: navy; }
  .box { border: 1px solid #889; padding: 10px 14px; background: #e9eefc; }
  a { color: #1a5; }
  p { margin: 12px 0; }
</style></head>
<body>
<h1>CGI Counter</h1>
<p class="box">Hello, <b>%s</b>! The counter is <b>%d</b>.</p>
<p><a href="/inc">increment via link</a></p>
<form action="/inc"><input type="submit" value="Increment via form"></form>
<form action="/">
  Set name: <input type="text" name="name" value="%s" size="16">
  <input type="submit" value="Save">
</form>
</body></html>""" % (n, c, n))
