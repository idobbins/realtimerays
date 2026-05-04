"use client";

import { useEffect, useRef } from "react";
import * as THREE from "three";

function createSkyTexture() {
  const canvas = document.createElement("canvas");
  canvas.width = 2;
  canvas.height = 512;

  const context = canvas.getContext("2d");

  if (!context) {
    return new THREE.Color("#f8f8f8");
  }

  const gradient = context.createLinearGradient(0, 0, 0, canvas.height);
  gradient.addColorStop(0, "#dfe8f3");
  gradient.addColorStop(0.45, "#f7f7f5");
  gradient.addColorStop(1, "#fffaf1");
  context.fillStyle = gradient;
  context.fillRect(0, 0, canvas.width, canvas.height);

  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.mapping = THREE.EquirectangularReflectionMapping;

  return texture;
}

export function RenderScene() {
  const containerRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const container = containerRef.current;

    if (!container) {
      return;
    }

    const scene = new THREE.Scene();
    const skyTexture = createSkyTexture();
    scene.background = skyTexture;
    scene.environment = skyTexture instanceof THREE.Texture ? skyTexture : null;

    const camera = new THREE.PerspectiveCamera(34, 1, 0.1, 100);
    camera.position.set(3.1, 2.1, 5.7);
    camera.lookAt(0, 0.85, 0);

    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.05;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    renderer.domElement.className = "block size-full";
    container.appendChild(renderer.domElement);

    const disposables: Array<{ dispose: () => void }> = [];

    const sphereGeometry = new THREE.SphereGeometry(0.48, 64, 32);
    disposables.push(sphereGeometry);

    const sphereMaterials = [
      new THREE.MeshStandardMaterial({ color: "#e6ddd0", roughness: 0.78, metalness: 0.02 }),
      new THREE.MeshStandardMaterial({ color: "#768395", roughness: 0.32, metalness: 0.16 }),
      new THREE.MeshPhysicalMaterial({
        color: "#f6f4ef",
        roughness: 0.08,
        metalness: 0,
        transmission: 0.35,
        thickness: 0.45,
      }),
      new THREE.MeshStandardMaterial({ color: "#b9c3b1", roughness: 0.55, metalness: 0.04 }),
      new THREE.MeshStandardMaterial({ color: "#d8c1a5", roughness: 0.38, metalness: 0.05 }),
    ];
    disposables.push(...sphereMaterials);

    const spherePositions = [
      [-0.9, 0.48, 0],
      [0, 0.48, 0],
      [0.9, 0.48, 0],
      [-0.45, 1.28, 0],
      [0.45, 1.28, 0],
    ] as const;

    const stack = new THREE.Group();
    spherePositions.forEach(([x, y, z], index) => {
      const sphere = new THREE.Mesh(sphereGeometry, sphereMaterials[index]);
      sphere.position.set(x, y, z);
      sphere.castShadow = true;
      sphere.receiveShadow = true;
      stack.add(sphere);
    });
    scene.add(stack);

    const keyLight = new THREE.DirectionalLight("#fff8ee", 3.4);
    keyLight.position.set(3.5, 5.5, 4);
    keyLight.castShadow = true;
    keyLight.shadow.mapSize.set(2048, 2048);
    keyLight.shadow.camera.near = 0.5;
    keyLight.shadow.camera.far = 12;
    keyLight.shadow.camera.left = -4;
    keyLight.shadow.camera.right = 4;
    keyLight.shadow.camera.top = 4;
    keyLight.shadow.camera.bottom = -4;
    scene.add(keyLight);

    const fillLight = new THREE.HemisphereLight("#f7fbff", "#e8ded2", 1.8);
    scene.add(fillLight);

    const rimLight = new THREE.DirectionalLight("#dbeafe", 0.9);
    rimLight.position.set(-4, 2.5, -3);
    scene.add(rimLight);

    const ground = new THREE.Mesh(
      new THREE.CircleGeometry(2.65, 128),
      new THREE.ShadowMaterial({ color: "#111827", opacity: 0.08 }),
    );
    ground.position.y = 0;
    ground.rotation.x = -Math.PI / 2;
    ground.receiveShadow = true;
    scene.add(ground);
    disposables.push(ground.geometry, ground.material);

    const resize = () => {
      const { width, height } = container.getBoundingClientRect();

      renderer.setSize(width, height);
      camera.aspect = width / Math.max(height, 1);
      camera.updateProjectionMatrix();
    };

    const resizeObserver = new ResizeObserver(resize);
    resizeObserver.observe(container);
    resize();

    let frame = 0;
    const animate = () => {
      stack.rotation.y = Math.sin(performance.now() * 0.0002) * 0.18;
      renderer.render(scene, camera);
      frame = requestAnimationFrame(animate);
    };
    animate();

    return () => {
      cancelAnimationFrame(frame);
      resizeObserver.disconnect();
      renderer.dispose();
      disposables.forEach((disposable) => disposable.dispose());
      if (skyTexture instanceof THREE.Texture) {
        skyTexture.dispose();
      }
      renderer.domElement.remove();
    };
  }, []);

  return <div ref={containerRef} className="size-full" aria-label="Three.js render preview" />;
}
