IMAGE_TAG ?= xpu-works/fuzzgpu-artifacts
CONTAINER_NAME ?= fuzzgpu-new

docker:
	# use proxy:
	# docker build --build-arg https_proxy=http://127.0.0.1:7890 -t $(IMAGE_TAG) .
	docker build -t $(IMAGE_TAG) . 2>&1 | tee docker-build.log

run:
	docker run -it --name $(CONTAINER_NAME) --tmpfs /tmp:rw,size=600g,exec $(IMAGE_TAG) bash
