// Helper program to generate a two-factor authentication code;
// modified from original work by Markus Gutschke to remove some
// functions unnecessary to that purpose.
//
// Further modified to eliminate the internal Base32 decoding, instead accepting the raw secret bytes.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Helper program to generate a new secret for use in two-factor
// authentication.
//
// Copyright 2010 Google Inc.
// Author: Markus Gutschke
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string.h>
#include "generate.h"
#include "sha1.h"
#include "sha256.h"
#include "hmac.h"

int generateCode(uint8_t *secret, uint8_t secret_length, unsigned long tm) {
  uint8_t challenge[8];
  for (int i = 8; i--; tm >>= 8) {
    challenge[i] = tm;
  }

  // Compute the HMAC_SHA1 of the secrete and the challenge.
  uint8_t hash[SHA256_DIGEST_LENGTH];
  int offset;

  if (secret_length > 48) {
    hmac_sha256(secret, secret_length, challenge, 8, hash, SHA256_DIGEST_LENGTH);
    offset = hash[SHA256_DIGEST_LENGTH - 1] & 0xF;

  } else {
    hmac_sha1(secret, secret_length, challenge, 8, hash, SHA1_DIGEST_LENGTH);

    // Pick the offset where to sample our hash value for the actual verification
    // code.
    offset = hash[SHA1_DIGEST_LENGTH - 1] & 0xF;
  }

  // Compute the truncated hash in a byte-order independent loop.
  unsigned int truncatedHash = 0;
  for (int i = 0; i < 4; ++i) {
    truncatedHash <<= 8;
    truncatedHash  |= hash[offset + i];
  }

  // Truncate to a smaller number of digits.
  truncatedHash &= 0x7FFFFFFF;

  return truncatedHash;
}
