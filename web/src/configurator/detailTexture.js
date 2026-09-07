import { LinearFilter, LinearMipmapLinearFilter } from 'three'

// DataTexture defaults to nearest sampling without mipmaps. That makes fine
// fibres and surface normals sparkle as the camera moves, even with MSAA.
export function filterDetailTexture(texture) {
  texture.generateMipmaps = true
  texture.minFilter = LinearMipmapLinearFilter
  texture.magFilter = LinearFilter
  texture.needsUpdate = true
  return texture
}
