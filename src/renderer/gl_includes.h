#pragma once
// On Windows, use GLEW to load OpenGL 2.0+ function pointers.
// On Linux/Mesa, GL_GLEXT_PROTOTYPES exposes GL3.x signatures directly from libGL.so.
#ifdef _WIN32
#  include <GL/glew.h>
#else
#  define GL_GLEXT_PROTOTYPES
#  include <GL/gl.h>
#  include <GL/glext.h>
#endif
