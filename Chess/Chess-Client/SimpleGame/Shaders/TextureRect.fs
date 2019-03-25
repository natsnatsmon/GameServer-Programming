#version 330

layout(location=0) out vec4 FragColor;

in vec2 v_TexPos;
uniform vec4 u_Color;
uniform sampler2D u_Texture;

void main()
{
	FragColor = texture(u_Texture, v_TexPos)*u_Color;
}
