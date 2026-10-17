// Test-Stub für <GLES3/gl3.h>: Host-Vorschau des VR-Menüs ohne GL-Kontext.
// Alle Aufrufe laufen ins Leere; Statusabfragen melden Erfolg, Erzeuger liefern 1.
#pragma once

#include <cstdint>

typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef char GLchar;
typedef intptr_t GLsizeiptr;

enum : GLenum {
    GL_FALSE = 0, GL_TRUE = 1,
    GL_ARRAY_BUFFER = 0x8892, GL_BACK = 0x0405, GL_BLEND = 0x0BE2, GL_CLAMP_TO_EDGE = 0x812F,
    GL_COLOR_BUFFER_BIT = 0x4000, GL_COMPILE_STATUS = 0x8B81, GL_CULL_FACE = 0x0B44,
    GL_DEPTH_BUFFER_BIT = 0x0100, GL_DEPTH_TEST = 0x0B71, GL_DYNAMIC_DRAW = 0x88E8, GL_FLOAT = 0x1406,
    GL_FRAGMENT_SHADER = 0x8B30, GL_LINEAR = 0x2601, GL_LINEAR_MIPMAP_LINEAR = 0x2703,
    GL_LINK_STATUS = 0x8B82, GL_ONE = 1, GL_ONE_MINUS_SRC_ALPHA = 0x0303, GL_RGBA = 0x1908,
    GL_SRC_ALPHA = 0x0302, GL_STATIC_DRAW = 0x88E4, GL_TEXTURE_2D = 0x0DE1,
    GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_WRAP_S = 0x2802,
    GL_TEXTURE_WRAP_T = 0x2803, GL_TEXTURE0 = 0x84C0, GL_TRIANGLES = 0x0004,
    GL_UNPACK_ALIGNMENT = 0x0CF5, GL_UNSIGNED_BYTE = 0x1401, GL_VERTEX_SHADER = 0x8B31,
};

#define XRSHELL_GL_NOOP(name) template <class... A> inline void name(A...) {}
XRSHELL_GL_NOOP(glActiveTexture) XRSHELL_GL_NOOP(glAttachShader) XRSHELL_GL_NOOP(glBindBuffer)
XRSHELL_GL_NOOP(glBindTexture) XRSHELL_GL_NOOP(glBlendFuncSeparate) XRSHELL_GL_NOOP(glBufferData)
XRSHELL_GL_NOOP(glClear) XRSHELL_GL_NOOP(glClearColor) XRSHELL_GL_NOOP(glCompileShader)
XRSHELL_GL_NOOP(glCullFace) XRSHELL_GL_NOOP(glDeleteBuffers) XRSHELL_GL_NOOP(glDeleteProgram)
XRSHELL_GL_NOOP(glDeleteShader) XRSHELL_GL_NOOP(glDeleteTextures) XRSHELL_GL_NOOP(glDisable)
XRSHELL_GL_NOOP(glDrawArrays) XRSHELL_GL_NOOP(glEnable) XRSHELL_GL_NOOP(glEnableVertexAttribArray)
XRSHELL_GL_NOOP(glGenerateMipmap) XRSHELL_GL_NOOP(glGetShaderInfoLog) XRSHELL_GL_NOOP(glLinkProgram)
XRSHELL_GL_NOOP(glPixelStorei) XRSHELL_GL_NOOP(glShaderSource) XRSHELL_GL_NOOP(glTexImage2D)
XRSHELL_GL_NOOP(glTexParameteri) XRSHELL_GL_NOOP(glTexSubImage2D) XRSHELL_GL_NOOP(glUniform1f)
XRSHELL_GL_NOOP(glUniform1i) XRSHELL_GL_NOOP(glUniform2f) XRSHELL_GL_NOOP(glUniform3fv)
XRSHELL_GL_NOOP(glUniform4f) XRSHELL_GL_NOOP(glUniformMatrix4fv) XRSHELL_GL_NOOP(glUseProgram)
XRSHELL_GL_NOOP(glVertexAttribPointer)
#undef XRSHELL_GL_NOOP

template <class... A> inline GLuint glCreateShader(A...) { return 1; }
template <class... A> inline GLuint glCreateProgram(A...) { return 1; }
template <class... A> inline GLint glGetUniformLocation(A...) { return 0; }
inline void glGetShaderiv(GLuint, GLenum, GLint* v) { *v = GL_TRUE; }
inline void glGetProgramiv(GLuint, GLenum, GLint* v) { *v = GL_TRUE; }
inline void glGenTextures(GLsizei n, GLuint* t) { for (GLsizei i = 0; i < n; ++i) t[i] = 1; }
inline void glGenBuffers(GLsizei n, GLuint* b) { for (GLsizei i = 0; i < n; ++i) b[i] = 1; }
