# Runs before the file system image is built (buildfs, uploadfs, and the
# ota environment's uploadfs): the image is made from a copy of data/ in
# which the web pages' text files are gzipped. The web server serves
# page.js.gz for page.js by itself (with the right type and Content-Encoding),
# so pages load faster and take less of the 1 MB partition. data/ itself is
# left as it is; files outside www/ (the velocity table, which the firmware
# reads) aren't compressed.
import gzip
import os
import shutil

Import("env")  # noqa: F821  (SCons)

COMPRESSED = (".html", ".js", ".css", ".svg", ".json")

if {"buildfs", "uploadfs", "uploadfsota"} & set(COMMAND_LINE_TARGETS):  # noqa: F821
    source = env.subst("$PROJECT_DATA_DIR")  # noqa: F821
    staged = os.path.join(env.subst("$BUILD_DIR"), "data_gz")  # noqa: F821
    shutil.rmtree(staged, ignore_errors=True)
    shutil.copytree(source, staged)
    before = after = 0
    for folder, _, files in os.walk(os.path.join(staged, "www")):
        for name in files:
            path = os.path.join(folder, name)
            if not name.endswith(COMPRESSED):
                continue
            with open(path, "rb") as f:
                data = f.read()
            with open(path + ".gz", "wb") as out:
                # mtime 0: the same files give the same image.
                with gzip.GzipFile(filename="", mode="wb", fileobj=out, compresslevel=9, mtime=0) as z:
                    z.write(data)
            os.remove(path)
            before += len(data)
            after += os.path.getsize(path + ".gz")
    print(f"Web files gzipped: {before // 1024} KB -> {after // 1024} KB")
    env.Replace(PROJECT_DATA_DIR=staged)  # noqa: F821
