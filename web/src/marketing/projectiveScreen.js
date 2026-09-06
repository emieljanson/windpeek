const VERTEX_SHADER = `
  attribute vec2 a_position;
  void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
  }
`

const FRAGMENT_SHADER = `
  #extension GL_OES_standard_derivatives : enable
  #ifdef GL_FRAGMENT_PRECISION_HIGH
    precision highp float;
  #else
    precision mediump float;
  #endif
  uniform sampler2D u_texture;
  uniform mat3 u_inverse;
  uniform vec2 u_resolution;
  uniform float u_opacity;
  uniform float u_brightness;
  uniform float u_contrast;
  uniform float u_shadowSize;
  uniform float u_shadowOpacity;
  uniform float u_reflection;
  uniform vec3 u_reflectionColor;

  float random(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
  }

  void main() {
    vec2 pixel = vec2(gl_FragCoord.x, u_resolution.y - gl_FragCoord.y);
    vec3 mapped = u_inverse * vec3(pixel, 1.0);
    vec2 uv = mapped.xy / mapped.z;
    float signedEdge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    if (signedEdge < -0.01) discard;
    float antialiasWidth = max(fwidth(signedEdge) * 1.35, 0.00045);
    float screenMask = smoothstep(-antialiasWidth, antialiasWidth, signedEdge);

    vec4 ink = texture2D(u_texture, uv);
    ink.rgb = (ink.rgb - 0.5) * u_contrast + 0.5;
    ink.rgb *= u_brightness;

    float edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    float inset = 1.0 - smoothstep(0.0, max(u_shadowSize, 0.0001), edge);
    float directional = mix(0.48, 1.0, (1.0 - uv.y) * 0.62 + uv.x * 0.38);
    ink.rgb *= 1.0 - inset * u_shadowOpacity * directional;

    float sweep = smoothstep(0.05, 0.95, 1.0 - (uv.x * 0.68 + uv.y * 0.32));
    float reflectionMask = sweep * (0.34 + 0.66 * smoothstep(0.0, 0.16, uv.y));
    ink.rgb = mix(ink.rgb, u_reflectionColor, reflectionMask * u_reflection);

    float grain = (random(pixel) - 0.5) * 0.018;
    ink.rgb += grain;
    gl_FragColor = vec4(ink.rgb, u_opacity * screenMask);
  }
`

function compile(gl, type, source) {
  const shader = gl.createShader(type)
  gl.shaderSource(shader, source)
  gl.compileShader(shader)
  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    throw new Error(gl.getShaderInfoLog(shader) || 'Shader compilation failed')
  }
  return shader
}

function solveLinear(matrix, values) {
  const size = values.length
  const rows = matrix.map((row, index) => [...row, values[index]])
  for (let column = 0; column < size; column += 1) {
    let pivot = column
    for (let row = column + 1; row < size; row += 1) {
      if (Math.abs(rows[row][column]) > Math.abs(rows[pivot][column])) pivot = row
    }
    ;[rows[column], rows[pivot]] = [rows[pivot], rows[column]]
    const divisor = rows[column][column]
    for (let cell = column; cell <= size; cell += 1) rows[column][cell] /= divisor
    for (let row = 0; row < size; row += 1) {
      if (row === column) continue
      const factor = rows[row][column]
      for (let cell = column; cell <= size; cell += 1) rows[row][cell] -= factor * rows[column][cell]
    }
  }
  return rows.map((row) => row[size])
}

function homography(corners) {
  const source = [[0, 0], [1, 0], [1, 1], [0, 1]]
  const matrix = []
  const values = []
  source.forEach(([u, v], index) => {
    const { x, y } = corners[index]
    matrix.push([u, v, 1, 0, 0, 0, -x * u, -x * v])
    values.push(x)
    matrix.push([0, 0, 0, u, v, 1, -y * u, -y * v])
    values.push(y)
  })
  const h = solveLinear(matrix, values)
  return [h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], 1]
}

function invert3(m) {
  const [a, b, c, d, e, f, g, h, i] = m
  const A = e * i - f * h
  const B = c * h - b * i
  const C = b * f - c * e
  const D = f * g - d * i
  const E = a * i - c * g
  const F = c * d - a * f
  const G = d * h - e * g
  const H = b * g - a * h
  const I = a * e - b * d
  const determinant = a * A + b * D + c * G
  return [A, B, C, D, E, F, G, H, I].map((value) => value / determinant)
}

function glMatrix3(rowMajor) {
  return new Float32Array([
    rowMajor[0], rowMajor[3], rowMajor[6],
    rowMajor[1], rowMajor[4], rowMajor[7],
    rowMajor[2], rowMajor[5], rowMajor[8],
  ])
}

function hexToRgb(hex) {
  const value = Number.parseInt(hex.replace('#', ''), 16)
  return [(value >> 16) / 255, ((value >> 8) & 255) / 255, (value & 255) / 255]
}

export function createProjectiveScreen(canvas) {
  const gl = canvas.getContext('webgl', { alpha: true, antialias: true, premultipliedAlpha: false })
  if (!gl) throw new Error('WebGL is required for the perspective screen')
  if (!gl.getExtension('OES_standard_derivatives')) {
    throw new Error('This browser cannot smooth the perspective screen edge')
  }

  const program = gl.createProgram()
  let vertexShader
  let fragmentShader
  try {
    vertexShader = compile(gl, gl.VERTEX_SHADER, VERTEX_SHADER)
    fragmentShader = compile(gl, gl.FRAGMENT_SHADER, FRAGMENT_SHADER)
    gl.attachShader(program, vertexShader)
    gl.attachShader(program, fragmentShader)
    gl.linkProgram(program)
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program))
  } catch (error) {
    if (vertexShader) gl.deleteShader(vertexShader)
    if (fragmentShader) gl.deleteShader(fragmentShader)
    gl.deleteProgram(program)
    throw error
  }
  gl.useProgram(program)

  const buffer = gl.createBuffer()
  gl.bindBuffer(gl.ARRAY_BUFFER, buffer)
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1]), gl.STATIC_DRAW)
  const position = gl.getAttribLocation(program, 'a_position')
  gl.enableVertexAttribArray(position)
  gl.vertexAttribPointer(position, 2, gl.FLOAT, false, 0, 0)

  const texture = gl.createTexture()
  gl.bindTexture(gl.TEXTURE_2D, texture)
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR)
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR)
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE)
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE)

  const locations = Object.fromEntries([
    'u_inverse', 'u_resolution', 'u_opacity', 'u_brightness', 'u_contrast',
    'u_shadowSize', 'u_shadowOpacity', 'u_reflection', 'u_reflectionColor',
  ].map((name) => [name, gl.getUniformLocation(program, name)]))

  function setFrame({ data, width, height }) {
    gl.bindTexture(gl.TEXTURE_2D, texture)
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, data)
  }

  function draw(corners, finish) {
    gl.viewport(0, 0, canvas.width, canvas.height)
    gl.clearColor(0, 0, 0, 0)
    gl.clear(gl.COLOR_BUFFER_BIT)
    gl.useProgram(program)
    gl.uniformMatrix3fv(locations.u_inverse, false, glMatrix3(invert3(homography(corners))))
    gl.uniform2f(locations.u_resolution, canvas.width, canvas.height)
    gl.uniform1f(locations.u_opacity, finish.opacity)
    gl.uniform1f(locations.u_brightness, finish.brightness)
    gl.uniform1f(locations.u_contrast, finish.contrast)
    gl.uniform1f(locations.u_shadowSize, finish.shadowSize)
    gl.uniform1f(locations.u_shadowOpacity, finish.shadowOpacity)
    gl.uniform1f(locations.u_reflection, finish.reflection)
    gl.uniform3fv(locations.u_reflectionColor, hexToRgb(finish.reflectionColor))
    gl.drawArrays(gl.TRIANGLES, 0, 6)
  }

  function dispose() {
    gl.deleteTexture(texture)
    gl.deleteBuffer(buffer)
    gl.deleteShader(vertexShader)
    gl.deleteShader(fragmentShader)
    gl.deleteProgram(program)
  }

  return { setFrame, draw, dispose }
}
