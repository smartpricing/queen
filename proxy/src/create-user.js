#!/usr/bin/env node
import { createUser } from './db.js';
import { hashPassword } from './auth.js';
import readline from 'readline';

const rl = readline.createInterface({
  input: process.stdin,
  output: process.stdout
});

function question(prompt) {
  return new Promise((resolve) => {
    rl.question(prompt, resolve);
  });
}

async function main() {
  console.log('=== Queen Proxy - Create User ===\n');

  const username = await question('Username: ');
  
  if (!username || username.trim() === '') {
    console.error('Error: Username is required');
    process.exit(1);
  }

  const password = await question('Password: ');
  
  if (!password || password.length < 6) {
    console.error('Error: Password must be at least 6 characters');
    process.exit(1);
  }

  console.log('\nRoles:');
  console.log('  1) admin       - Full access');
  console.log('  2) read-write  - Read-write access');
  console.log('  3) read-only   - Read-only access');
  
  const roleChoice = await question('\nSelect role (1-3): ');
  
  const roleMap = {
    '1': 'admin',
    '2': 'read-write',
    '3': 'read-only'
  };

  const role = roleMap[roleChoice.trim()];

  if (!role) {
    console.error('Error: Invalid role selection');
    process.exit(1);
  }

  try {
    const passwordHash = await hashPassword(password);
    const user = await createUser(username.trim(), passwordHash, role);
    
    console.log('\n✓ User created successfully!');
    console.log(`  Username: ${user.username}`);
    console.log(`  Role: ${user.role}`);
    console.log(`  ID: ${user.id}`);
  } catch (error) {
    if (error.code === '23505') { // Unique constraint violation
      console.error('\nError: Username already exists');
    } else {
      console.error('\nError creating user:', error.message);
    }
    process.exit(1);
  } finally {
    rl.close();
    process.exit(0);
  }
}

main();

