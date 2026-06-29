# syntax=docker/dockerfile:1

FROM node:22-alpine AS build
WORKDIR /app

ARG VITE_API_URL=https://netwatch-arsen-demo.ru
ARG VITE_REALTIME_URL=wss://netwatch-arsen-demo.ru/ws
ENV VITE_API_URL=${VITE_API_URL}
ENV VITE_REALTIME_URL=${VITE_REALTIME_URL}

COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci

COPY frontend/index.html frontend/tsconfig*.json frontend/vite.config.ts ./
COPY frontend/src ./src
RUN npm run build

FROM nginx:1.27-alpine
COPY frontend/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /app/dist /usr/share/nginx/html
EXPOSE 80
