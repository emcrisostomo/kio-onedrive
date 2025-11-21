# Rate limits & enterprise app registration

This KIO worker uses the Microsoft Graph API via a **multi-tenant public Azure app registration**. All users who select the standard “Microsoft” provider in KDE Online Accounts share the same Azure **client ID**.

## Service protection limits (rate limiting)

Microsoft Graph enforces **service protection limits** to keep tenants and the service stable. When those limits are exceeded, Graph may return:

* `429 Too Many Requests`
* `503 Service Unavailable`

often with a `Retry-After` header.

The KIO worker is designed to behave like a “good citizen”:

* It **caches** directory listings and metadata to avoid unnecessary calls.
* It limits the number of **concurrent requests**.
* On `429` / `503`, it **honors `Retry-After`** and backs off instead of hammering the API.

In practice, for normal interactive file manager usage, you should not notice rate limits at all. When they do occur, operations may be delayed and you may see temporary errors like “OneDrive is being rate-limited; retrying later”.

---

## Using your own Azure app registration (enterprise / heavy usage)

Some organizations prefer to use their **own** Azure app registration for policy, monitoring, or capacity reasons. KAccounts supports this via **custom providers**, so your traffic can be isolated behind your own client ID and tenant.

### 1. Create your Azure app registration

In the Azure portal (Entra ID):

1. Create a new **“Public client / native”** app registration (no client secret required).

2. Configure redirect URI, e.g.:

   * `http://localhost/oauth2callback`

   (Use whatever this project’s docs say if they differ.)

3. Under **API permissions**, add at minimum:

   * `Files.ReadWrite.All` (delegated)
   * `offline_access` (delegated)

4. Decide whether this app is:

   * **Single-tenant** (only your organization), or
   * **Multi-tenant** (any org / Microsoft account), depending on your needs.

Keep note of:

* **Client ID (Application ID)**
* **Tenant ID** (if you’re using a specific tenant, not `common`)

---

### 2. Create a custom KAccounts provider

On the KDE side, you can create a **custom provider** that points to your own app registration.

> You can do this system-wide (recommended for managed environments) or per-user.

#### Per-user override example

1. Locate the existing Microsoft provider file (path may vary by distro), e.g.:

   ```bash
   ls /usr/share/accounts/providers
   ```

   You should see something like `microsoft.provider` or `kde-microsoft.provider`.

2. Copy it into your local providers directory:

   ```bash
   mkdir -p ~/.local/share/accounts/providers
   cp /usr/share/accounts/providers/microsoft.provider \
      ~/.local/share/accounts/providers/microsoft-yourorg.provider
   ```

3. Edit `~/.local/share/accounts/providers/microsoft-yourorg.provider` and:

   * Change the provider **id** and **display name** to something unique, e.g.:

     ```ini
     [Provider]
     Id=microsoft-yourorg
     Name=Microsoft (YourOrg)
     ```

   * Update the OAuth section (names may vary slightly by distro), e.g.:

     ```ini
     [OAuth]
     ClientId=YOUR_APP_CLIENT_ID
     Tenant=YOUR_TENANT_ID_OR_COMMON
     ```

   The exact keys and format can differ slightly depending on the KAccounts version; use the original file as a reference and only modify the client/tenant bits.

4. (Optional but recommended) Copy the OneDrive service file to target your new provider, for better UX with this KIO worker:

   ```bash
   mkdir -p ~/.local/share/accounts/services
   cp /usr/share/accounts/services/kde/microsoft-onedrive.service \
      ~/.local/share/accounts/services/microsoft-onedrive-yourorg.service
   ```

   Then edit `microsoft-onedrive-yourorg.service` so that it points to your new provider id:

   ```ini
   [Service]
   ProviderId=microsoft-yourorg
   Name=Microsoft OneDrive (YourOrg)
   ```

Now, in **System Settings/Online Accounts**, you should see an entry like:

* **Microsoft (YourOrg)**

If you create an account using that entry, this KIO worker will use your **own client ID and tenant**, so any throttling and policies are now under your organization’s control.

---

### 3. Recommended deployment pattern

For most users:

* The **default KDE Microsoft provider** (multi-tenant public app) is recommended and should be sufficient.

For enterprises / heavy tenants:

* Provide a small package (or configuration management script) that:

  * Installs a custom `microsoft-yourorg.provider`,
  * Optionally installs a matching `microsoft-onedrive-yourorg.service`.

Then instruct your users:

> “Use **Microsoft (YourOrg)** in Online Accounts for OneDrive access.”

This setup keeps the project’s default app simple and user-friendly, while giving organizations a clean, supported way to use their own Azure app registration and capacity envelope.
