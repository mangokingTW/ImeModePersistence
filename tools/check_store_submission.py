import os
import sys
import json
import urllib.request
import urllib.parse
import urllib.error

sys.stdout.reconfigure(encoding="utf-8")
sys.stderr.reconfigure(encoding="utf-8")

def get_token(tenant_id, client_id, client_secret, resource):
    url = f"https://login.microsoftonline.com/{tenant_id}/oauth2/token"
    data = urllib.parse.urlencode({
        "grant_type": "client_credentials",
        "client_id": client_id,
        "client_secret": client_secret,
        "resource": resource
    }).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/x-www-form-urlencoded"})
    with urllib.request.urlopen(req) as resp:
        res = json.loads(resp.read().decode("utf-8"))
        return res["access_token"]

def api_request(url, method="GET", token=None, body=None):
    headers = {
        "Authorization": f"Bearer {token}",
        "Content-Type": "application/json; charset=utf-8"
    }
    data = json.dumps(body).encode("utf-8") if body is not None else None
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req) as resp:
            content = resp.read().decode("utf-8")
            return json.loads(content) if content else {}
    except urllib.error.HTTPError as e:
        err_body = e.read().decode("utf-8", errors="replace")
        print(f"API Error {e.code} on {url}: {err_body}")
        return {"error": e.code, "message": err_body}

def main():
    tenant_id = os.environ["PARTNER_CENTER_TENANT_ID"]
    client_id = os.environ["PARTNER_CENTER_CLIENT_ID"]
    client_secret = os.environ["PARTNER_CENTER_CLIENT_SECRET"]
    app_id = "9P05QQZ2P5XC"

    print("=== Querying Microsoft Partner Center Live Status ===")
    token = get_token(tenant_id, client_id, client_secret, "https://manage.devcenter.microsoft.com")
    base_url = f"https://manage.devcenter.microsoft.com/v1.0/my/applications/{app_id}"

    app_details = api_request(base_url, token=token)
    print("Application Primary Name:", app_details.get("primaryName"))
    print("Application ID:", app_details.get("id"))
    print("hasAdvancedListingPermission:", app_details.get("hasAdvancedListingPermission"))

    pending_sub = app_details.get("pendingApplicationSubmission")
    last_published_sub = app_details.get("lastPublishedApplicationSubmission")

    print("\n--- Current Pending (In-Progress / In-Certification) Submission ---")
    if pending_sub:
        pending_id = pending_sub.get("id")
        print(f"Pending Submission ID: {pending_id}")
        
        status_res = api_request(f"{base_url}/submissions/{pending_id}/status", token=token)
        print("Submission Status:", json.dumps(status_res, indent=2, ensure_ascii=False))

        sub_data = api_request(f"{base_url}/submissions/{pending_id}", token=token)
        trailers = sub_data.get("trailers", [])
        print(f"Trailers Attached (Count: {len(trailers)}):")
        for idx, t in enumerate(trailers, 1):
            print(f"  Trailer #{idx}: videoFileName = {t.get('videoFileName')}")
            for loc, asset in t.get("trailerAssets", {}).items():
                print(f"    - Locale [{loc}]: Title='{asset.get('title')}', Images={len(asset.get('imageList', []))}")
    else:
        print("No pending submission currently in flight.")

    print("\n--- Last Published Submission ---")
    if last_published_sub:
        last_id = last_published_sub.get("id")
        print(f"Last Published Submission ID: {last_id}")
        sub_data = api_request(f"{base_url}/submissions/{last_id}", token=token)
        trailers = sub_data.get("trailers", [])
        print(f"Trailers Attached in Last Published (Count: {len(trailers)}):")
        for idx, t in enumerate(trailers, 1):
            print(f"  Trailer #{idx}: videoFileName = {t.get('videoFileName')}")
    else:
        print("No last published submission found.")

if __name__ == "__main__":
    main()
