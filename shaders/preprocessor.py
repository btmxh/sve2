import os
import sys
# a dead simple shader preprocessor, with
# - #include support (only #include "/path/to/something" for simplicity)
# - additional defines (-DA=B), include dirs (-I/include/dir)
# - specify version (-V "300 es")
# after all options, specify the input and output files:
# python preprocessor.py -DA=B -I/some/dir -V "300 es" input.glsl output.glsl

if len(sys.argv) < 3:
    print("usage: python preprocessor.py [options] [input_path] [output_path]")
    exit(1)

defines: list[tuple[str, str]] = []
include_paths: list[str] = []
version = None
i = 1
while i < len(sys.argv) - 2:
    arg = sys.argv[i]
    if arg.startswith("-D"):
        define = arg.removeprefix("-D").split("=")
        defines.append((define[0], define[1] if len(define) == 2 else ""))
        i += 1
        continue
    if arg.startswith("-I"):
        include_paths.append(arg.removeprefix("-I"))
        i += 1
        continue
    if arg.startswith("-V"):
        version = sys.argv[i + 1]
        i += 2
        continue
    raise ValueError("invalid option: " + arg)

input_path = sys.argv[-2]
output_path = sys.argv[-1]

if version is None:
    raise ValueError("version not specified")
if input_path.endswith(".vert.glsl"):
    defines.append(("SVE2_VERTEX_SHADER", ""))
if input_path.endswith(".frag.glsl"):
    defines.append(("SVE2_FRAGMENT_SHADER", ""))

output_lines: list[str] = []
output_lines.append(f"#version {version}\n")
for (key, value) in defines:
    output_lines.append(f"#define {key} {value}\n")


def resolve_include(file: str, paths: list[str]):
    for path in paths:
        potential_path = os.path.join(path, file)
        if os.path.exists(potential_path):
            return potential_path
    return None


def preprocess_shader(input_path: str, include_paths: list[str], pragma_onces: set[str]) -> list[str]:
    if input_path in pragma_onces:
        return []
    processed_code: list[str] = []
    with open(input_path, 'r') as file:
        for line in file:
            if line.strip().startswith('#include'):
                include_file = line.split('"')[1].strip()
                include_path = resolve_include(
                    include_file, [os.path.dirname(input_path)] + include_paths)
                if include_path:
                    processed_code.extend(preprocess_shader(
                        include_path, include_paths, pragma_onces))
                else:
                    raise FileNotFoundError(
                        f"Include file {include_file} not found.")
            elif line.strip() == "#pragma once":
                pragma_onces.add(input_path)
            else:
                processed_code.append(line)
    return processed_code


output_lines.extend(preprocess_shader(input_path, include_paths, set()))
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, 'w') as file:
    file.writelines(output_lines)
