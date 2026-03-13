import os

svg_files = [f for f in os.listdir('.') if f.endswith('.svg')]
svg_files.sort()

html_start = """<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Alfred SVG Expressions</title>
    <style>
        body {
            background-color: #1a1a1a;
            color: #ffffff;
            font-family: Arial, sans-serif;
            display: flex;
            flex-wrap: wrap;
            gap: 20px;
            padding: 20px;
            justify-content: center;
        }
        .container {
            display: flex;
            flex-direction: column;
            align-items: center;
            background-color: #000000;
            border-radius: 12px;
            padding: 10px;
            width: 340px; /* 320 + padding */
            box-shadow: 0 4px 8px rgba(0,0,0,0.5);
            border: 1px solid #333;
        }
        .svg-preview {
            width: 320px;
            height: 240px;
            display: flex;
            align-items: center;
            justify-content: center;
            background-color: #000000;
        }
        .svg-preview svg {
            width: 320px;
            height: 240px;
        }
        p {
            margin: 10px 0 0 0;
            font-size: 14px;
            text-transform: capitalize;
            color: #39ff14;
        }
    </style>
</head>
<body>
"""

html_end = """
</body>
</html>
"""

with open('preview.html', 'w') as out:
    out.write(html_start)
    for f in svg_files:
        name = f[:-4]
        with open(f, 'r') as svg:
            content = svg.read()
        out.write('    <div class="container">\n')
        out.write('        <div class="svg-preview">\n')
        out.write(content + '\n')
        out.write('        </div>\n')
        out.write(f'        <p>{name}</p>\n')
        out.write('    </div>\n')
    out.write(html_end)
