# ============================================
# 阶段 1：前端构建
# ============================================
FROM node:20-alpine AS frontend-builder
WORKDIR /app
COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci
COPY frontend/ ./
RUN npm run build

# ============================================
# 阶段 2：后端构建（Linux 原生编译）
# ============================================
FROM alpine:3.21 AS backend-builder
RUN apk add --no-cache g++ cmake make openssl-dev
WORKDIR /app
COPY include/ ./include/
COPY src/ ./src/
COPY CMakeLists.txt ./
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release
RUN cmake --build build

# ============================================
# 阶段 3：前端运行时（nginx）
# ============================================
FROM nginx:alpine AS frontend
COPY --from=frontend-builder /app/dist /usr/share/nginx/html
COPY frontend/nginx.conf /etc/nginx/conf.d/default.conf
EXPOSE 80

# ============================================
# 阶段 4：后端运行时
# ============================================
FROM alpine:3.21 AS backend
RUN apk add --no-cache openssl libstdc++
COPY --from=backend-builder /app/build/proxy /usr/local/bin/proxy
EXPOSE 8080
CMD ["proxy"]
