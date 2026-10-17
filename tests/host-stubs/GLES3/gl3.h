// Test-Stub für <GLES3/gl3.h>: Host-Vorschau des VR-Menüs (tests/host/menu_preview.cpp) ohne
// GL-Kontext. Die Menütafel wird auf der CPU gerastert; alle GL-Aufrufe laufen ins Leere,
// Shader/Programme gelten als erfolgreich gebaut.
#pragma once

#include <cstddef>
#include <cstdint>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef char GLchar;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef std::ptrdiff_t GLsizeiptr;
typedef std::ptrdiff_t GLintptr;

enum : GLenum {
    GL_FALSE = 0, GL_TRUE = 1,
    GL_TRIANGLES = 0x0004, GL_ONE = 1, GL_SRC_ALPHA = 0x0302, GL_ONE_MINUS_SRC_ALPHA = 0x0303,
    GL_BACK = 0x0405, GL_CULL_FACE = 0x0B44, GL_DEPTH_TEST = 0x0B71, GL_BLEND = 0x0BE2,
    GL_UNPACK_ALIGNMENT = 0x0CF5, GL_TEXTURE_2D = 0x0DE1, GL_UNSIGNED_BYTE = 0x1401, GL_FLOAT = 0x1406,
    GL_RGBA = 0x1908, GL_LINEAR = 0x2601, GL_LINEAR_MIPMAP_LINEAR = 0x2703,
    GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_WRAP_S = 0x2802,
    GL_TEXTURE_WRAP_T = 0x2803, GL_CLAMP_TO_EDGE = 0x812F, GL_TEXTURE0 = 0x84C0,
    GL_ARRAY_BUFFER = 0x8892, GL_STATIC_DRAW = 0x88E4, GL_DYNAMIC_DRAW = 0x88E8,
    GL_FRAGMENT_SHADER = 0x8B30, GL_VERTEX_SHADER = 0x8B31, GL_COMPILE_STATUS = 0x8B81,
    GL_LINK_STATUS = 0x8B82, GL_DEPTH_BUFFER_BIT = 0x00000100, GL_COLOR_BUFFER_BIT = 0x00004000,
};

#define XR_GL_STUB(name) template <typename... A> inline void name(A...) {}
XR_GL_STUB(glActiveTexture) XR_GL_STUB(glAttachShader) XR_GL_STUB(glBindBuffer)
XR_GL_STUB(glBindTexture) XR_GL_STUB(glBlendFuncSeparate) XR_GL_STUB(glBufferData)
XR_GL_STUB(glClear) XR_GL_STUB(glClearColor) XR_GL_STUB(glCompileShader) XR_GL_STUB(glCullFace)
XR_GL_STUB(glDeleteBuffers) XR_GL_STUB(glDeleteProgram) XR_GL_STUB(glDeleteShader)
XR_GL_STUB(glDeleteTextures) XR_GL_STUB(glDisable) XR_GL_STUB(glDrawArrays) XR_GL_STUB(glEnable)
XR_GL_STUB(glEnableVertexAttribArray) XR_GL_STUB(glGenerateMipmap) XR_GL_STUB(glLinkProgram)
XR_GL_STUB(glPixelStorei) XR_GL_STUB(glShaderSource) XR_GL_STUB(glTexImage2D)
XR_GL_STUB(glTexParameteri) XR_GL_STUB(glTexSubImage2D) XR_GL_STUB(glUniform1f) XR_GL_STUB(glUniform1i)
XR_GL_STUB(glUniform2f) XR_GL_STUB(glUniform3fv) XR_GL_STUB(glUniform4f) XR_GL_STUB(glUniformMatrix4fv)
XR_GL_STUB(glUseProgram) XR_GL_STUB(glVertexAttribPointer) XR_GL_STUB(glGetShaderInfoLog)
#undef XR_GL_STUB

inline GLuint glCreateShader(GLenum) { return 1; }
inline GLuint glCreateProgram() { return 1; }
inline GLint glGetUniformLocation(GLuint, const GLchar*) { return -1; }
inline void glGetShaderiv(GLuint, GLenum, GLint* v) { if (v) *v = GL_TRUE; }
inline void glGetProgramiv(GLuint, GLenum, GLint* v) { if (v) *v = GL_TRUE; }
inline void glGenBuffers(GLsizei n, GLuint* ids) { for (GLsizei i = 0; i < n; ++i) ids[i] = 1; }
inline void glGenTextures(GLsizei n, GLuint* ids) { for (GLsizei i = 0; i < n; ++i) ids[i] = 1; }
