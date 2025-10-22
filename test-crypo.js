import { decryptPayload } from './client-js/services/encryptionService.js';

// One of the production messages
const prodMessage = {
    "id": "019a01cc-bfb2-72d0-bcda-6c7a1465cc99",
    "payload": "{\"iv\": \"KPZ1SegvBPFuu8wqASTWSA==\", \"authTag\": \"hebs442ygZKojGCUn3tXMw==\", \"encrypted\": \"VVdHpWCVdpGFyVM6NtkSIX+a3Qq4qvrH5KeYeeF5nDxXPfpARcW1RyZ\/kd7DzwgO7DUHOU28sh6TJhdvRPD1hGLyQvNCWfU7R9KgDosTLoNmJuCCuQvULDryltRy7zCH\"}"
};

// Parse the payload
const encrypted = JSON.parse(prodMessage.payload);
console.log('Testing production message decryption:');
console.log('  iv:', encrypted.iv);
console.log('  authTag:', encrypted.authTag);
console.log('  encrypted (first 60 chars):', encrypted.encrypted.substring(0, 60) + '...');

try {
    const decrypted = await decryptPayload(encrypted);
    console.log('\n✅ SUCCESS: Decrypted production message!');
    console.log('Decrypted data:', JSON.stringify(decrypted).substring(0, 200) + '...');
} catch (error) {
    console.log('\n❌ FAILED: Could not decrypt production message');
    console.log('Error:', error.message);
    console.log('\n🔑 This confirms the QUEEN_ENCRYPTION_KEY in prod is different from the one you have locally!');
    console.log('The production messages were encrypted with a different key.');
}
