const { parentPort, workerData } = require("node:worker_threads");
const native = require(workerData.addon);
parentPort.on("message", ({ id, request }) => {
  try {
    parentPort.postMessage({
      id,
      result: JSON.parse(native.invoke(JSON.stringify(request))),
    });
  } catch (error) {
    parentPort.postMessage({ id, error: error.message });
  }
});
