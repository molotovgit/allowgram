import { test, expect } from '@playwright/test';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { randomUUID, randomBytes } from 'node:crypto';
const root = fileURLToPath(new URL('../../', import.meta.url));

test('consenting device appears automatically, owner verifies and approves, ACK is separate', async ({page, request}) => {
  test.setTimeout(60000);
  const data = process.env.HEAD_E2E_DATA_DIR!;
  const codePath = join(data, `code-${randomUUID()}.txt`);
  execFileSync(join(root, '.venv/bin/python'), ['-m','headboard.cli','--data',data,'owner-login','--output',codePath],{cwd:root});
  await page.goto('/');
  await page.getByLabel('Access code',{exact:true}).fill(readFileSync(codePath,'utf8').trim());
  await page.getByRole('button',{name:'Open control board'}).click();
  await expect(page.getByRole('heading',{name:'Managed users',exact:true})).toBeVisible();
  const token = randomBytes(32).toString('base64url');
  const auth = {Authorization:`Bearer ${token}`};
  const registered = await request.post('/api/client/register',{headers:auth,data:{
    telegram_user_id:'91001',consent_version:1,device_name:'Synthetic consent client',
    client_version:'synthetic-browser-QA',initial_peers:[{kind:'user',id:'23456'}],
  }});
  expect(registered.status()).toBe(200);
  const connection = await registered.json();
  const row = page.getByTestId(`connection-${connection.id}`);
  await expect(row).toBeVisible({timeout:20000});
  await expect(row).toContainText('Unverified');
  await expect(row).toContainText('91001');
  await expect(row).toContainText(connection.fingerprint);
  await expect(row.getByRole('button',{name:'Approve connection'})).toBeDisabled();
  expect((await request.get('/api/client/policy',{headers:auth})).status()).toBe(401);
  await row.getByRole('checkbox').check();
  await row.getByRole('button',{name:'Approve connection'}).click();
  await expect(row).not.toBeVisible();
  await page.getByRole('button',{name:'Allowgram user 91001',exact:true}).click();
  await expect(page.getByRole('heading',{name:'Allowgram user 91001',exact:true})).toBeVisible();
  await expect(page.getByText('Awaiting sync',{exact:true}).first()).toBeVisible();
  const approved = await request.get('/api/client/registration',{headers:auth});
  expect((await approved.json()).status).toBe('approved');
  expect(await page.locator('body').innerText()).not.toContain(token);
});
