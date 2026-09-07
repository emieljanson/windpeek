import { describe, expect, it, vi } from 'vitest'
import { createProjectiveScreen } from '../src/marketing/projectiveScreen'

function webGlContext() {
  let nextId = 0
  return {
    VERTEX_SHADER: 0x8B31,
    FRAGMENT_SHADER: 0x8B30,
    COMPILE_STATUS: 0x8B81,
    LINK_STATUS: 0x8B82,
    ARRAY_BUFFER: 0x8892,
    STATIC_DRAW: 0x88E4,
    FLOAT: 0x1406,
    TEXTURE_2D: 0x0DE1,
    TEXTURE_MIN_FILTER: 0x2801,
    TEXTURE_MAG_FILTER: 0x2800,
    TEXTURE_WRAP_S: 0x2802,
    TEXTURE_WRAP_T: 0x2803,
    LINEAR: 0x2601,
    CLAMP_TO_EDGE: 0x812F,
    RGBA: 0x1908,
    UNSIGNED_BYTE: 0x1401,
    COLOR_BUFFER_BIT: 0x4000,
    TRIANGLES: 0x0004,
    createShader: vi.fn(() => ({ id: ++nextId })),
    shaderSource: vi.fn(),
    compileShader: vi.fn(),
    getShaderParameter: vi.fn(() => true),
    getShaderInfoLog: vi.fn(() => ''),
    deleteShader: vi.fn(),
    createProgram: vi.fn(() => ({ id: ++nextId })),
    attachShader: vi.fn(),
    linkProgram: vi.fn(),
    getProgramParameter: vi.fn(() => true),
    getProgramInfoLog: vi.fn(() => ''),
    useProgram: vi.fn(),
    deleteProgram: vi.fn(),
    createBuffer: vi.fn(() => ({ id: ++nextId })),
    bindBuffer: vi.fn(),
    bufferData: vi.fn(),
    getAttribLocation: vi.fn(() => 0),
    enableVertexAttribArray: vi.fn(),
    vertexAttribPointer: vi.fn(),
    deleteBuffer: vi.fn(),
    createTexture: vi.fn(() => ({ id: ++nextId })),
    bindTexture: vi.fn(),
    texParameteri: vi.fn(),
    texImage2D: vi.fn(),
    deleteTexture: vi.fn(),
    getUniformLocation: vi.fn((_program, name) => name),
    getExtension: vi.fn(() => ({})),
    viewport: vi.fn(),
    clearColor: vi.fn(),
    clear: vi.fn(),
    uniformMatrix3fv: vi.fn(),
    uniform2f: vi.fn(),
    uniform1f: vi.fn(),
    uniform3fv: vi.fn(),
    drawArrays: vi.fn(),
  }
}

describe('projective screen', () => {
  it('falls back to mediump fragment precision and releases shader resources', () => {
    const gl = webGlContext()
    const canvas = { getContext: vi.fn(() => gl) }

    const screen = createProjectiveScreen(canvas)
    const fragmentSource = gl.shaderSource.mock.calls.find(
      ([shader]) => shader === gl.createShader.mock.results[1].value,
    )[1]

    expect(fragmentSource).toContain('#ifdef GL_FRAGMENT_PRECISION_HIGH')
    expect(fragmentSource).toContain('precision mediump float;')
    expect(canvas.getContext).toHaveBeenCalledWith('webgl', {
      alpha: true,
      antialias: true,
      premultipliedAlpha: true,
    })
    expect(fragmentSource).toContain('vec4(ink.rgb * alpha, alpha)')

    screen.dispose()

    expect(gl.deleteShader).toHaveBeenCalledTimes(2)
    expect(gl.deleteTexture).toHaveBeenCalledOnce()
    expect(gl.deleteBuffer).toHaveBeenCalledOnce()
    expect(gl.deleteProgram).toHaveBeenCalledOnce()
  })
})
