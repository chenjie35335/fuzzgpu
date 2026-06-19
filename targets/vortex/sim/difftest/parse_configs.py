import sys
import subprocess

defs = []
cflags = []
i = 1
arch, config_vh = None, None
while i < len(sys.argv):
    arg = sys.argv[i]
    if arg[:2] == '-D':
        defs.append(arg[2:])
        cflags.append(arg)

    elif arg[:2] == '-I':
        cflags.append(arg)

    elif arg == '--pcfg-arch':
        i += 1
        if i >= len(sys.argv):
            break
        arch = sys.argv[i]
    elif arg == '--pcfg-config-vh':
        i += 1
        if i >= len(sys.argv):
            break
        config_vh = sys.argv[i] 
    i += 1

if arch is None:
    print('Missing --pcfg-arch value')
    sys.exit(1)
if config_vh is None:
    print('Missing --pcfg-config-vh value')
    sys.exit(1)

defs = set(defs)

# expand the `if `else macros
macros = []
with open(config_vh, 'r') as fp:
    for line in fp:
        line = line.strip()
        if line[:len('`define')] == '`define':
            tokens = line[len('`define'):].strip()
            i = 0
            while i < len(tokens):
                if tokens[i].isspace() or tokens[i] == '(':
                    break
                i += 1
            j=i
            while j < len(tokens):
                if not tokens[j].isspace():
                    break
                j += 1
            if j < len(tokens) and tokens[j] == '(':
                continue # skip macro()
            macros.append(tokens[:i])

# construct the sv that will evaluate all the parameters
tmp_file = 'config.sv.tmp'
with open(tmp_file, 'w') as fp:
    fp.write(f'''
`include "{config_vh}"\n')

module config();

''')
    for macro in macros:
        fp.write(f'logic longint {macro} = `{macro};\n')
    fp.write('endmodule\n')

result = subprocess.run(
    ["verilator", "-E", "--pp-comments"] + cflags + [tmp_file],
    stdout=subprocess.PIPE, 
    stderr=subprocess.PIPE, 
    text=True               
)

if result.returncode:
    print('STDERR: ', result.stderr)
    print('STDOUT: ', result.stdout)
    sys.exit(result.returncode)

print(result.stdout)
