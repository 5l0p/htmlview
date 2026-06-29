#!/usr/bin/env python3
import os, sys
path = os.environ.get("PATH_INFO", "/")
method = os.environ.get("REQUEST_METHOD", "GET")
if path == "/login" and method == "POST":
    n = int(os.environ.get("CONTENT_LENGTH", "0") or "0")
    body = sys.stdin.read(n)
    open("/tmp/login_post.txt", "w").write(body)   # for test verification
    print("Content-Type: text/html\n")
    print("<title>OK</title><h1>Logged in</h1><pre>%s</pre>" % body)
else:
    print("Content-Type: text/html\n")
    print("""<title>Login</title><h1>Login</h1>
<form method="post" action="/login">
Username: <input type="text" name="user" value="claude" size="16"><br>
Password: <input type="password" name="pw" value="secret" size="16"><br>
<input type="radio" name="plan" value="free" checked> Free
<input type="radio" name="plan" value="pro"> Pro<br>
<input type="checkbox" name="news" checked> News<br>
<input type="submit" value="Log in">
</form>""")
