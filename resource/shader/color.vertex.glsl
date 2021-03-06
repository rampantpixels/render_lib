attribute vec4 position;
attribute vec4 color;

uniform mat4 transform_mvp;

varying vec4 frag_color;

void main(void) {
	frag_color = color;
	gl_Position = position * transform_mvp;
}
