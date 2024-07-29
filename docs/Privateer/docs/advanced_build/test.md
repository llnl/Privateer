# Test Privateer for Development

There are two types of tests programs in Metall.

## Google Test

### Build

To run tests using Google Test,
please see [this page](./cmake.md) about building the test programs.

### Run

```bash
make test
```

The tests create datastores in the `./datastore` directory by default.

To change the location, use an environmental value 'PRIVATEER_TEST_DIR'.

```bash
env PRIVATEER_TEST_DIR="/mnt/ssd/" make test
```
