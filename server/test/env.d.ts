declare namespace Cloudflare {
  interface Env {
    DB: D1Database;
    BLOBS: R2Bucket;
    TEST_MIGRATIONS: { name: string; queries: string[] }[];
  }
}
