// @ts-check
const { test, expect } = require('@playwright/test');

test('sqlite loads', async ({ page }) => {
  page.on('pageerror', (error) => {
    throw error;
  });

  await page.goto('http://localhost:8080/demo-123.html');
  await page.getByText(`That's all, folks!`).waitFor({timeout: 10000});
});
