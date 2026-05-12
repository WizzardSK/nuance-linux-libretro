#ifndef SHADERPROGRAM_H
#define SHADERPROGRAM_H

#include "basetypes.h"
#include <GL/glew.h>

class ShaderProgram final
{
public:
  ShaderProgram();
  ~ShaderProgram();

  bool Initialize();

  bool Uninitalize();

  // Drop GL handles without calling glDelete* — for use after the GL
  // context they belonged to has already been destroyed.
  void ForgetHandles();

  bool InstallShaderSourceFromFile(const char * const filename, GLenum type);
  bool InstallShaderSourceFromMemory(char **sourceStrings, uint32 count, const int *lengths, GLenum type);
  bool CompileShader(GLenum type);
  bool Link();
  void PrintInfoLog(GLhandleARB obj, const char *msg);
  bool AttachShader(GLenum type);
  bool DetachShader(GLenum type);
  bool CompileAndLinkShaders();
  bool StartShaderProgram();
  bool StopShaderProgram();
  GLhandleARB GetProgramObject() { return hProgramObject; }
  GLhandleARB GetVertexShaderObject() { return hVertexShaderObject; }
  GLhandleARB GetFragmentShaderObject() { return hFragmentShaderObject; }

private:
  GLhandleARB hVertexShaderObject;
  GLhandleARB hFragmentShaderObject;
  GLhandleARB hProgramObject;
  bool bVertexShaderObjectAttached;
  bool bFragmentShaderObjectAttached;
  bool bVertexShaderCodeLoaded;
  bool bFragmentShaderCodeLoaded;
};

#endif
