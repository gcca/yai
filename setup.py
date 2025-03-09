#!/usr/bin/env python

import os
from urllib.request import urlretrieve

from setuptools import Extension, setup

GITHUB_REPO = "https://raw.githubusercontent.com/gcca/xai-cpp/master/"

for f in ["xai.hpp", "xai.cpp"]:
    file_path = os.path.join("yai_ext", f)
    if not os.path.isfile(file_path):
        print(f"Descargando {f} desde GitHub...")
        url = f"{GITHUB_REPO}{f}"
        try:
            urlretrieve(url, file_path)
            print(f"   Descargado {f} exitosamente.")
        except Exception as e:
            raise RuntimeError(f"   No se pudo descargar {f} desde {url}: {e}")

YAI_EXT_DIR = "yai_ext"

BOOST_INCLUDE_DIR = "/usr/include"
BOOST_LIB_DIR = "/usr/lib/x86_64-linux-gnu"
BOOST_LIBRARIES = ["boost_system", "boost_json"]

OPENSSL_INCLUDE_DIR = "/usr/include"
OPENSSL_LIB_DIR = "/usr/lib/x86_64-linux-gnu"
OPENSSL_LIBRARIES = ["ssl", "crypto"]

if "BOOST_ROOT" in os.environ:
    BOOST_INCLUDE_DIR = os.path.join(os.environ["BOOST_ROOT"], "include")
    BOOST_LIB_DIR = os.path.join(os.environ["BOOST_ROOT"], "lib")

if "OPENSSL_ROOT" in os.environ:
    OPENSSL_INCLUDE_DIR = os.path.join(os.environ["OPENSSL_ROOT"], "include")
    OPENSSL_LIB_DIR = os.path.join(os.environ["OPENSSL_ROOT"], "lib")

SOURCES = ["yai-chat-abi.cc", "xai.cpp", "yAi.cpp"]

setup(
    name="yai_ext",
    version="0.1",
    ext_modules=[
        Extension(
            "yai_chat_abi",
            sources=[os.path.join(YAI_EXT_DIR, f) for f in SOURCES],
            include_dirs=[YAI_EXT_DIR, BOOST_INCLUDE_DIR, OPENSSL_INCLUDE_DIR],
            library_dirs=[BOOST_LIB_DIR, OPENSSL_LIB_DIR],
            libraries=BOOST_LIBRARIES + OPENSSL_LIBRARIES,
            extra_compile_args=[
                "-std=c++23",
                "-O3",
                "-DNDEBUG",
                "-march=native",
                "-g0",
            ],
            extra_link_args=["-flto"],
        )
    ],
)
