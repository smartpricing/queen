#!/usr/bin/env node

/**
 * Compatibility Test for Queen C++ Server
 * 
 * This script tests the C++ server against the existing Node.js test suite
 * to ensure API compatibility.
 */

const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');

// Configuration
const CPP_SERVER_PORT = 6633;
const CPP_SERVER_HOST = 'localhost';
const TEST_TIMEOUT = 60000; // 60 seconds

console.log('🧪 Queen C++ Compatibility Test Suite');
console.log('=====================================\n');

// Check if C++ server binary exists
const serverBinary = path.join(__dirname, 'bin', 'queen-server');
if (!fs.existsSync(serverBinary)) {
    console.error('❌ C++ server binary not found at:', serverBinary);
    console.error('💡 Run "make" to build the server first');
    process.exit(1);
}

// Check if Node.js test exists
const nodeTestPath = path.join(__dirname, '..', 'src', 'test', 'test-new.js');
if (!fs.existsSync(nodeTestPath)) {
    console.error('❌ Node.js test suite not found at:', nodeTestPath);
    process.exit(1);
}

console.log('✅ Found C++ server binary');
console.log('✅ Found Node.js test suite');
console.log('');

let cppServer = null;
let testProcess = null;

// Cleanup function
function cleanup() {
    console.log('\n🧹 Cleaning up...');
    
    if (testProcess) {
        testProcess.kill('SIGTERM');
    }
    
    if (cppServer) {
        cppServer.kill('SIGTERM');
        setTimeout(() => {
            if (cppServer && !cppServer.killed) {
                cppServer.kill('SIGKILL');
            }
        }, 2000);
    }
}

// Handle process termination
process.on('SIGINT', cleanup);
process.on('SIGTERM', cleanup);
process.on('exit', cleanup);

async function waitForServer(host, port, timeout = 10000) {
    const net = require('net');
    const start = Date.now();
    
    return new Promise((resolve, reject) => {
        const tryConnect = () => {
            const socket = new net.Socket();
            
            socket.setTimeout(1000);
            
            socket.on('connect', () => {
                socket.destroy();
                resolve();
            });
            
            socket.on('error', () => {
                socket.destroy();
                if (Date.now() - start > timeout) {
                    reject(new Error(`Server not ready after ${timeout}ms`));
                } else {
                    setTimeout(tryConnect, 500);
                }
            });
            
            socket.on('timeout', () => {
                socket.destroy();
                if (Date.now() - start > timeout) {
                    reject(new Error(`Server not ready after ${timeout}ms`));
                } else {
                    setTimeout(tryConnect, 500);
                }
            });
            
            socket.connect(port, host);
        };
        
        tryConnect();
    });
}

async function runTest() {
    try {
        // Start C++ server
        console.log('🚀 Starting C++ Queen server...');
        cppServer = spawn(serverBinary, [
            '--port', CPP_SERVER_PORT.toString(),
            '--host', CPP_SERVER_HOST,
            '--dev'
        ], {
            stdio: ['pipe', 'pipe', 'pipe'],
            env: {
                ...process.env,
                // Ensure we use test database settings
                PG_DB: process.env.PG_DB || 'postgres',
                PG_HOST: process.env.PG_HOST || 'localhost',
                PG_USER: process.env.PG_USER || 'postgres',
                PG_PASSWORD: process.env.PG_PASSWORD || 'postgres'
            }
        });
        
        let serverOutput = '';
        let serverReady = false;
        
        cppServer.stdout.on('data', (data) => {
            const output = data.toString();
            serverOutput += output;
            
            // Check if server is ready
            if (output.includes('Ready to process messages') || output.includes('Listening on port')) {
                serverReady = true;
            }
            
            // Log server output with prefix
            output.split('\n').forEach(line => {
                if (line.trim()) {
                    console.log(`[C++ Server] ${line}`);
                }
            });
        });
        
        cppServer.stderr.on('data', (data) => {
            const output = data.toString();
            serverOutput += output;
            
            // Log server errors with prefix
            output.split('\n').forEach(line => {
                if (line.trim()) {
                    console.log(`[C++ Server ERROR] ${line}`);
                }
            });
        });
        
        cppServer.on('error', (error) => {
            console.error('❌ Failed to start C++ server:', error.message);
            process.exit(1);
        });
        
        cppServer.on('exit', (code, signal) => {
            if (code !== 0 && code !== null) {
                console.error(`❌ C++ server exited with code ${code}`);
                if (!serverReady) {
                    console.error('Server output:', serverOutput);
                }
            }
        });
        
        // Wait for server to be ready
        console.log('⏳ Waiting for server to be ready...');
        await waitForServer(CPP_SERVER_HOST, CPP_SERVER_PORT, 15000);
        console.log('✅ C++ server is ready\n');
        
        // Run Node.js test suite against C++ server
        console.log('🧪 Running Node.js test suite against C++ server...');
        console.log('================================================\n');
        
        testProcess = spawn('node', [nodeTestPath], {
            stdio: ['pipe', 'pipe', 'pipe'],
            cwd: path.join(__dirname, '..'),
            env: {
                ...process.env,
                QUEEN_TEST_PORT: CPP_SERVER_PORT.toString(),
                QUEEN_TEST_HOST: CPP_SERVER_HOST,
                // Override base URL to point to C++ server
                QUEEN_BASE_URL: `http://${CPP_SERVER_HOST}:${CPP_SERVER_PORT}`
            }
        });
        
        let testOutput = '';
        let testsPassed = false;
        
        testProcess.stdout.on('data', (data) => {
            const output = data.toString();
            testOutput += output;
            process.stdout.write(output);
            
            // Check for test completion indicators
            if (output.includes('All tests passed') || output.includes('✅')) {
                testsPassed = true;
            }
        });
        
        testProcess.stderr.on('data', (data) => {
            const output = data.toString();
            testOutput += output;
            process.stderr.write(output);
        });
        
        // Set test timeout
        const testTimeout = setTimeout(() => {
            console.error('\n❌ Test timeout after', TEST_TIMEOUT / 1000, 'seconds');
            testProcess.kill('SIGTERM');
        }, TEST_TIMEOUT);
        
        testProcess.on('exit', (code, signal) => {
            clearTimeout(testTimeout);
            
            console.log('\n================================================');
            
            if (code === 0) {
                console.log('✅ All tests passed! C++ server is compatible.');
                console.log('🎉 The C++ implementation successfully passes the Node.js test suite.');
            } else {
                console.log(`❌ Tests failed with exit code ${code}`);
                console.log('💡 Check the test output above for details.');
                
                if (signal) {
                    console.log(`Test process was killed with signal: ${signal}`);
                }
            }
            
            cleanup();
            process.exit(code);
        });
        
        testProcess.on('error', (error) => {
            clearTimeout(testTimeout);
            console.error('❌ Failed to run test suite:', error.message);
            cleanup();
            process.exit(1);
        });
        
    } catch (error) {
        console.error('❌ Test setup failed:', error.message);
        cleanup();
        process.exit(1);
    }
}

// Quick health check first
async function healthCheck() {
    console.log('🏥 Performing quick health check...');
    
    try {
        // Start server briefly to check if it can start
        const healthServer = spawn(serverBinary, ['--help'], {
            stdio: ['pipe', 'pipe', 'pipe']
        });
        
        return new Promise((resolve, reject) => {
            let output = '';
            
            healthServer.stdout.on('data', (data) => {
                output += data.toString();
            });
            
            healthServer.on('exit', (code) => {
                if (code === 0 && output.includes('Usage:')) {
                    console.log('✅ C++ server binary is functional');
                    resolve();
                } else {
                    reject(new Error('C++ server binary appears to be broken'));
                }
            });
            
            healthServer.on('error', (error) => {
                reject(error);
            });
        });
        
    } catch (error) {
        throw new Error(`Health check failed: ${error.message}`);
    }
}

// Run the compatibility test
async function main() {
    try {
        await healthCheck();
        await runTest();
    } catch (error) {
        console.error('❌ Compatibility test failed:', error.message);
        cleanup();
        process.exit(1);
    }
}

main();
