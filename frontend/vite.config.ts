import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Dev-сервер на 5173 — совпадает с GATEWAY_CORS_ORIGIN по умолчанию.
export default defineConfig({
  plugins: [react()],
  server: { port: 5173 },
});
