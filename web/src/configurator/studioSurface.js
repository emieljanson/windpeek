import * as THREE from 'three'

export function createLitFloor(stage) {
  const material = new THREE.MeshStandardMaterial({ color: 0x505052, roughness: 0.92, metalness: 0, envMapIntensity: 0.12, transparent: true, depthWrite: false })
  material.onBeforeCompile = (shader) => {
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', '#include <common>\nvarying vec2 vFloorPosition;')
      .replace('#include <begin_vertex>', '#include <begin_vertex>\nvFloorPosition = position.xy;')
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', '#include <common>\nvarying vec2 vFloorPosition;')
      .replace('#include <color_fragment>', '#include <color_fragment>\ndiffuseColor.a *= 1.0 - smoothstep(0.08, 0.38, length(vFloorPosition));')
      .replace('#include <opaque_fragment>', 'outgoingLight *= 0.8;\n#include <opaque_fragment>')
  }
  const floor = new THREE.Mesh(new THREE.PlaneGeometry(20, 20), material)
  floor.name = 'STUDIO_LIT_FLOOR'
  floor.renderOrder = -2
  floor.rotation.x = -Math.PI / 2
  floor.position.y = stage.surfaceY - 0.0001
  floor.receiveShadow = true
  return floor
}

export function applyStudioSurfaceTheme(scene, dark) {
  const floor = scene.getObjectByName('STUDIO_LIT_FLOOR')
  if (floor) floor.visible = dark
  const shadow = scene.getObjectByName('PHYSICAL_SHADOW_LAYER')
  if (shadow) shadow.visible = !dark
  const contact = scene.getObjectByName('CONTACT_OCCLUSION')
  if (contact) contact.material.uniforms.contactColor.value.setRGB(...(dark ? [0, 0, 0] : [0.075, 0.082, 0.078]))
  const grid = scene.getObjectByName('SURFACE_GRID')
  if (grid) {
    grid.visible = true
    grid.material.uniforms.lineColor.value.set(dark ? 0x7f7f81 : 0x6f7784)
    grid.material.uniforms.lineOpacity.value = dark ? 0.20 / 1.4 : 0.24
    grid.material.uniforms.stageFadeStart.value = dark ? 0.055 : 0.28
    grid.material.uniforms.stageFadeEnd.value = dark ? 0.22 : 0.82
  }
}

export function createPerspectiveSurface(stage) {
  const gridMaterial = new THREE.ShaderMaterial({
    name: 'perspective-line-surface',
    transparent: true,
    depthWrite: false,
    toneMapped: false,
    extensions: { derivatives: true },
    uniforms: {
      horizonFadeEnd: { value: 1.35 },
      horizonFadeStart: { value: 0.58 },
      lineColor: { value: new THREE.Color(0x6f7784) },
      lineOpacity: { value: 0.24 },
      spacing: { value: 0.032 },
      stageFadeEnd: { value: 0.82 },
      stageFadeStart: { value: 0.28 },
    },
    vertexShader: `
      varying vec3 vWorldPosition;

      void main() {
        vec4 worldPosition = modelMatrix * vec4(position, 1.0);
        vWorldPosition = worldPosition.xyz;
        gl_Position = projectionMatrix * viewMatrix * worldPosition;
      }
    `,
    fragmentShader: `
      uniform vec3 lineColor;
      uniform float lineOpacity;
      uniform float spacing;
      uniform float horizonFadeStart;
      uniform float horizonFadeEnd;
      uniform float stageFadeStart;
      uniform float stageFadeEnd;
      varying vec3 vWorldPosition;

      void main() {
        vec2 coordinate = vWorldPosition.xz / spacing;
        vec2 footprint = max(fwidth(coordinate), vec2(0.0001));
        vec2 distanceToLine = abs(fract(coordinate - 0.5) - 0.5) / footprint;
        float line = 1.0 - min(min(distanceToLine.x, distanceToLine.y), 1.0);
        float cellSizeInPixels = 1.0 / max(footprint.x, footprint.y);
        float densityFade = smoothstep(3.5, 9.0, cellSizeInPixels);
        float cameraDistance = distance(cameraPosition, vWorldPosition);
        float horizonFade = 1.0 - smoothstep(horizonFadeStart, horizonFadeEnd, cameraDistance);
        float stageFade = 1.0 - smoothstep(stageFadeStart, stageFadeEnd, length(vWorldPosition.xz));

        float gridAlpha = line * densityFade * horizonFade * stageFade * stageFade * lineOpacity;
        gl_FragColor = vec4(lineColor, gridAlpha);
      }
    `,
  })
  const grid = new THREE.Mesh(new THREE.PlaneGeometry(8, 8), gridMaterial)
  grid.name = 'SURFACE_GRID'
  grid.rotation.x = -Math.PI / 2
  grid.position.y = stage.surfaceY
  grid.renderOrder = -1
  return grid
}

export function createPhysicalShadowLayer(stage, opacity = 0.28) {
  const material = new THREE.ShadowMaterial({
    color: 0x4d524f,
    opacity,
    transparent: true,
    depthWrite: false,
    toneMapped: false,
  })
  const shadowLayer = new THREE.Mesh(new THREE.PlaneGeometry(0.72, 0.52), material)
  shadowLayer.name = 'PHYSICAL_SHADOW_LAYER'
  shadowLayer.rotation.x = -Math.PI / 2
  shadowLayer.position.y = stage.shadowY
  shadowLayer.receiveShadow = true
  shadowLayer.renderOrder = 0
  return shadowLayer
}

export function createContactOcclusion(stage, opacityScale = 1) {
  const material = new THREE.ShaderMaterial({
    name: 'contact-occlusion',
    transparent: true,
    depthWrite: false,
    toneMapped: false,
    uniforms: {
      contactOpacity: { value: stage.contactOpacity * opacityScale },
      contactPower: { value: stage.contactPower },
      contactColor: { value: new THREE.Color().setRGB(0.075, 0.082, 0.078) },
    },
    vertexShader: `
      varying vec2 vUv;
      void main() {
        vUv = uv;
        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
      }
    `,
    fragmentShader: `
      varying vec2 vUv;
      uniform float contactOpacity;
      uniform float contactPower;
      uniform vec3 contactColor;
      void main() {
        float ends = 1.0 - smoothstep(0.462, 0.5, abs(vUv.x - 0.5));
        float frontTail = smoothstep(0.0, 0.5, vUv.y);
        float backTail = 1.0 - smoothstep(0.5, 1.0, vUv.y);
        float contact = vUv.y < 0.5 ? frontTail : backTail;
        contact = pow(max(contact, 0.0), contactPower);
        gl_FragColor = vec4(contactColor, ends * contact * contactOpacity);
      }
    `,
  })
  const contact = new THREE.Mesh(
    new THREE.PlaneGeometry(stage.contactWidth, stage.contactDepth),
    material,
  )
  contact.name = 'CONTACT_OCCLUSION'
  contact.rotation.x = -Math.PI / 2
  // BODY_03 touches the surface across x ±87.5 mm and z 0…4 mm. Keep the
  // shader's end fade outside that footprint so the shadow reaches beneath
  // both rounded corners instead of disappearing just before them.
  contact.position.set(0, stage.contactY, stage.contactZ)
  contact.renderOrder = 1
  return contact
}
