import { decryptPayload } from './client-js/services/encryptionService.js';

const messages = [
    {
        id: "019a01cc-bfb2-72d0-bcda-6c7a1465cc99",
        date: "2025-10-20T13:26:26.985Z",  // Oct 20 - C++
        payload: "{\"iv\": \"KPZ1SegvBPFuu8wqASTWSA==\", \"authTag\": \"hebs442ygZKojGCUn3tXMw==\", \"encrypted\": \"VVdHpWCVdpGFyVM6NtkSIX+a3Qq4qvrH5KeYeeF5nDxXPfpARcW1RyZ/kd7DzwgO\"}"
    },
    {
        id: "0199ed8d-83c0-7382-9f81-1ac418aad7b8",
        date: "2025-10-16T15:04:58.558Z",  // Oct 16 - Node.js
        payload: "{\"iv\": \"IasiGla+7NlaSZP+Eog1lQ==\", \"authTag\": \"9IB/NkHNsqD9KhblO9G5UA==\", \"encrypted\": \"y+tbi2C/FEgZQYFs/fnqSaF1L/gGtfZXfo3DLKyc0/jCGFOI4J7GSEIkg2PmUufO\"}"
    },
    {
        id: "0199ed8c-e72f-7404-8111-e317a3786c91",
        date: "2025-10-16T15:04:18.468Z",  // Oct 16 - Node.js
        payload: "{\"iv\": \"RUcb8BQrrcihGhfYOHUqRw==\", \"authTag\": \"0mTSHorkHbvcA90aul3Sug==\", \"encrypted\": \"jfp81tuXTp7/VdlXUZyUzGoPKutsIaslVd+7zzfHANljA2AsOu63uwvB2KwIHIe0\"}"
    },
    {
        id: "0199ed65-b2c5-7421-b8b1-03336eb52fd6",
        date: "2025-10-16T14:21:29.152Z",  // Oct 16 - Node.js
        payload: "{\"iv\": \"hqXjc7uR4aBqysCZPEfzyA==\", \"authTag\": \"jbHfb7Y3E3k8SEeh+9xY6A==\", \"encrypted\": \"+6vDhdY8an3F8RFlNmkqci30gu+eQ1NWU1LemiruAM+oG4q2jTiS1Wk/BuTf8ej9\"}"
    },
    {
        id: "0199ed62-b2c1-7300-85fd-dcac8c35c7f9",
        date: "2025-10-16T14:18:12.538Z",  // Oct 16 - Node.js
        payload: "{\"iv\": \"vYvcWgF0YfDh/SfNtp4UEQ==\", \"authTag\": \"izqsH7Fh0Tj2la4Ihl4RWQ==\", \"encrypted\": \"KugcafwaYDm/8zD9haO4yVxivoGiJWbet/NOr8VIjpri8+Uu1+iCxI2zYqpbkhmN\"}"
    }
];

console.log('Testing all production messages with key:', process.env.QUEEN_ENCRYPTION_KEY ? 'SET' : 'NOT SET');
console.log('='.repeat(80));

for (const msg of messages) {
    const encrypted = JSON.parse(msg.payload);
    console.log(`\nMessage ID: ${msg.id}`);
    console.log(`Date: ${msg.date} (${msg.date.startsWith('2025-10-20') ? 'C++' : 'Node.js'})`);
    console.log(`  iv: ${encrypted.iv}`);
    console.log(`  authTag: ${encrypted.authTag}`);
    console.log(`  encrypted (first 40): ${encrypted.encrypted.substring(0, 40)}...`);
    
    try {
        const decrypted = await decryptPayload(encrypted);
        console.log(`  ✅ DECRYPTED: ${JSON.stringify(decrypted).substring(0, 100)}...`);
    } catch (error) {
        console.log(`  ❌ FAILED: ${error.message}`);
    }
}

console.log('\n' + '='.repeat(80));

