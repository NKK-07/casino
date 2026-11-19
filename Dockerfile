# --- build stage: compile with g++ on Linux and run the tests ---
FROM gcc:13 AS build
WORKDIR /src
COPY include ./include
COPY src ./src
COPY tests ./tests
RUN g++ -std=c++17 -O2 -Wall -Wextra -Iinclude src/*.cpp -o casino -pthread -static-libstdc++ -static-libgcc \
 && g++ -std=c++17 -O2 -Iinclude $(ls src/*.cpp | grep -v main.cpp) tests/tests.cpp -o casino_tests -pthread \
 && ./casino_tests

# --- runtime stage: small image with just the binary and web files ---
FROM debian:bookworm-slim
WORKDIR /app
COPY --from=build /src/casino /app/casino
COPY web /app/web
RUN mkdir -p /app/data
# Mount a persistent volume here to keep players, history and logs across deploys.
VOLUME ["/app/data"]
ENV PORT=8080
EXPOSE 8080
# Railway (and most hosts) inject $PORT; bind to all interfaces.
CMD ["sh", "-c", "exec /app/casino --web --host 0.0.0.0 --port ${PORT:-8080} --web-dir /app/web --data /app/data"]
