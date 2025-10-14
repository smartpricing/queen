<template>
  <object :data="logoUrl" type="image/svg+xml" aria-label="Queen Logo"></object>
</template>

<script setup>
// Beautiful Queen logo - a feminine duck with crown managing queues
// Using external SVG file for easier maintenance and reusability
// Using <object> tag to preserve SVG animations
import logoUrl from '../../../assets/queen-logo.svg'
</script>

<style scoped>
object {
  pointer-events: none;
  width: 100%;
  height: 100%;
}
</style>

