#version 430

void main(void)
{
  if (gl_VertexIndex == 0) {
    gl_Position = vec4(0.0f, 1000.0f, 0.0f, 1.0f);
  } else if (gl_VertexIndex == 1) {
    gl_Position = vec4(1000.0f, -1000.0f, 0.0f, 1.0f);
  } else if (gl_VertexIndex == 2) {
    gl_Position = vec4(-1000.0f, -1000.0f, 0.0f, 1.0f);
  }
}