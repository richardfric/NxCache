# NxCache Web Service

This file describes the endpoints and authentication for the NxCache web service, based on the [OpenAPI spec](https://nx.dev/docs/guides/tasks--caching/self-hosted-caching).

## Endpoints

### GET /v1/cache/{hash}
- **Description:** Download a task output
- **Path Parameter:**
  - `hash` (string, required): The hash of the task output to retrieve
- **Responses:**
  - 200: Returns the cache artifact as an octet stream
  - 401: Missing or invalid authentication token (text/plain)
  - 403: Access forbidden (text/plain)
  - 404: Record not found
- **Security:** Optional bearer token

### PUT /v1/cache/{hash}
- **Description:** Upload a task output
- **Path Parameter:**
  - `hash` (string, required): The hash of the task output to upload
- **Header Parameter:**
  - `Content-Length` (number, required): The file size in bytes
- **Request Body:**
  - application/octet-stream (binary)
- **Responses:**
  - 200: Successfully uploaded
  - 400: Missing or invalid Content-Length header (text/plain)
  - 401: Missing or invalid authentication token (text/plain)
  - 403: Access forbidden (text/plain)
  - 409: Cannot override an existing record
- **Security:** Optional bearer token

## Security
- **Type:** HTTP Bearer Token
- **Description:** Auth mechanism for all endpoints

## Notes
- All endpoints can optionally use authentication via a bearer token in the `Authorization` header.
- The service should return appropriate status codes and error messages as described above.
- The service accepts only loopback connections.
