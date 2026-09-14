# `xrpld` Docker Image

`xrpld` is published to Docker Hub as [`xrplf/xrpld`](https://hub.docker.com/r/xrplf/xrpld):
the `xrpld` DEB package installed on Ubuntu 26.04, running as the `xrpld` user.
Each release is tagged with its version, `xrplf/xrpld:<version>`, and
`xrplf/xrpld:develop` follows the `develop` branch.
See [`package/README.md`](../package/README.md#docker-images) for how it is built
and tagged.

```bash
docker run --detach --name xrpld \
    --volume xrpld-db:/var/lib/xrpld \
    --publish 2459:2459 \
    xrplf/xrpld:develop
```

The admin ports (5005, 6006 and 50051) listen on `127.0.0.1` in the shipped
configuration; to reach them from outside the container, mount your own over
`/etc/xrpld/xrpld.cfg`.
