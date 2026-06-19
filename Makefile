IMAGE_TAG ?= xpu-works/fuzzgpu-artifacts
CONTAINER_NAME ?= fuzzgpu-new
PROXY_HOST ?= host.docker.internal
PROXY_PORT ?= 7890
PROXY_URL ?= http://$(PROXY_HOST):$(PROXY_PORT)
DOCKER_HOST_GATEWAY ?= --add-host=host.docker.internal:host-gateway

docker:
	docker build -t $(IMAGE_TAG) . 2>&1 | tee docker-build.log

docker-proxy:
	docker build $(DOCKER_HOST_GATEWAY) --build-arg http_proxy=$(PROXY_URL) --build-arg https_proxy=$(PROXY_URL) -t $(IMAGE_TAG) . 2>&1 | tee docker-build.log

run:
	docker run -it --name $(CONTAINER_NAME) --tmpfs /tmp:rw,size=600g,exec $(IMAGE_TAG) bash
