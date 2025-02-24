#version 430

layout(location = 0) out vec4 out_fragColor;

vec4 brickTexture(vec2 uv, vec2 brickSize, vec2 mortarSize, vec4 brickColor, vec4 mortarColor) {
    // Adjust UV coordinates to account for mortar size
    vec2 brickAndMortar = brickSize + mortarSize;
    vec2 scaledUV = uv / brickAndMortar;

    // Calculate the position within the current brick cell
    vec2 brickCell = floor(scaledUV);
    vec2 cellUV = fract(scaledUV);

    // Offset every other row to create a staggered brick pattern
    if (mod(brickCell.y, 2.0) < 1.0) {
        cellUV.x += 0.5; // Shift every other row
    }

    // Adjust cellUV to account for the staggered offset
    cellUV.x = fract(cellUV.x);

    // Determine if the current fragment is in the brick or mortar
    vec2 mortarThreshold = mortarSize / brickAndMortar;
    bool isBrick = cellUV.x > mortarThreshold.x && cellUV.y > mortarThreshold.y;

    // Return the appropriate color
    return isBrick ? brickColor : mortarColor;
}

void main()
{
    vec2 uv = (gl_FragCoord.xy - vec2(100) / 2.0) / vec2(100);
    vec2 brickSize = vec2(0.2, 0.05); // Size of each brick
    vec2 mortarSize = vec2(0.01, 0.01); // Size of mortar lines
    vec4 brickColor = vec4(1, 1, 0.2, 1.0); // Main Bill's skin color
    vec4 mortarColor = vec4(0.9, 0.8, 0.2, 1.0); // Mortar is slightly darker
    out_fragColor = brickTexture(uv, brickSize, mortarSize, brickColor, mortarColor);
}
