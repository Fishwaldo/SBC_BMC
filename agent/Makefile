.PHONY: compile
PROTOC_GEN_GO := $(GOPATH)/bin/protoc-gen-go

# If $GOPATH/bin/protoc-gen-go does not exist, we'll run this command to install
# it.
#$(PROTOC_GEN_GO):
#	go get -u google.golang.org/protobuf/cmd/protoc-gen-go@latest

#pkg/generated/espmsg.pb.go: proto/espmsg.proto | $(PROTOC_GEN_GO)
pkg/generated/espmsg.pb.go: ../proto/espmsg.proto
	protoc --go_out=. ../proto/espmsg.proto

# This is a "phony" target - an alias for the above command, so "make compile"
# still works.
compile: pkg/generated/espmsg.pb.go
