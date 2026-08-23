import os
import sys
import json
import zipfile
import pathlib
import urllib.request
import urllib.parse
import urllib.error

def get_access_token(tenant_id, client_id, client_secret):
    url = f"https://login.microsoftonline.com/{tenant_id}/oauth2/v2.0/token"
    data = urllib.parse.urlencode({
        "grant_type": "client_credentials",
        "client_id": client_id,
        "client_secret": client_secret,
        "scope": "https://api.partner.microsoft.com/.default"
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
    with urllib.request.urlopen(req) as resp:
        return json.loads(resp.read().decode("utf-8"))

def upload_zip_to_blob(file_upload_url, zip_path):
    print(f"Uploading {zip_path} ({os.path.getsize(zip_path)} bytes) to Azure Blob Storage...")
    with open(zip_path, "rb") as f:
        data = f.read()
    req = urllib.request.Request(
        file_upload_url,
        data=data,
        headers={
            "x-ms-blob-type": "BlockBlob",
            "Content-Type": "application/zip"
        },
        method="PUT"
    )
    with urllib.request.urlopen(req) as resp:
        print(f"Blob upload finished with status code: {resp.status}")

def main():
    tenant_id = os.environ["PARTNER_CENTER_TENANT_ID"]
    client_id = os.environ["PARTNER_CENTER_CLIENT_ID"]
    client_secret = os.environ["PARTNER_CENTER_CLIENT_SECRET"]
    msix_path = pathlib.Path(os.environ["MSIX_PATH"])
    video_path = pathlib.Path("packaging/store/store-preview.mp4")
    thumb_path = pathlib.Path("packaging/store/store-preview-thumb.png")
    app_id = "9P05QQZ2P5XC"

    print("Authenticating with Azure AD...")
    token = get_access_token(tenant_id, client_id, client_secret)

    base_url = f"https://manage.devcenter.microsoft.com/v1.0/my/applications/{app_id}"

    # 1. Check existing submissions
    print("Checking existing submissions...")
    try:
        sub_info = api_request(f"{base_url}/submissions", token=token)
        print("Submissions list:", sub_info)
    except Exception as e:
        print("Failed to list submissions, fetching app details:", e)

    # Delete any pending draft if exists
    try:
        app_details = api_request(base_url, token=token)
        if "pendingApplicationSubmission" in app_details and app_details["pendingApplicationSubmission"]:
            pending_id = app_details["pendingApplicationSubmission"]["id"]
            print(f"Deleting pending draft submission {pending_id}...")
            urllib.request.urlopen(urllib.request.Request(
                f"{base_url}/submissions/{pending_id}",
                headers={"Authorization": f"Bearer {token}"},
                method="DELETE"
            ))
    except Exception as e:
        print("No pending submission to delete or delete failed:", e)

    # 2. Create a new draft submission
    print("Creating new draft submission...")
    sub = api_request(f"{base_url}/submissions", method="POST", token=token)
    sub_id = sub["id"]
    file_upload_url = sub["fileUploadUrl"]
    print(f"Created submission ID: {sub_id}")

    # 3. Create zip package with MSIX + 1080p MP4 + 1080p PNG
    zip_path = pathlib.Path("submission_assets.zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.write(msix_path, arcname=msix_path.name)
        zf.write(video_path, arcname="store-preview.mp4")
        zf.write(thumb_path, arcname="store-preview-thumb.png")
    print("Packed submission zip containing:", zf.namelist())

    # 4. Upload zip to Azure Blob Storage
    upload_zip_to_blob(file_upload_url, zip_path)

    # 5. Update submission JSON with packages and 5-language trailers
    sub["applicationPackages"] = [
        {
            "fileName": msix_path.name,
            "fileStatus": "PendingUpload",
            "minimumDirectXVersion": "None",
            "minimumSystemRam": "None"
        }
    ]

    titles = {
        "zh-tw": "即時輸入法模式維持與游標指示器動態演示",
        "zh-hant": "即時輸入法模式維持與游標指示器動態演示",
        "zh-cn": "实时输入法模式保持与光标指示器动态演示",
        "zh-hans": "实时输入法模式保持与光标指示器动态演示",
        "ja-jp": "アプリごとのIME入力モード維持＆カーソルインジケーター実演",
        "ja": "アプリごとのIME入力モード維持＆カーソルインジケーター実演",
        "ko-kr": "앱별 IME 입력 모드 유지 및 커서 표시기 실시간 시연",
        "ko": "앱별 IME 입력 모드 유지 및 커서 표시기 실시간 시연"
    }

    if "listings" in sub:
        for key, listing in sub["listings"].items():
            base_listing = listing.get("baseListing", {})
            k_lower = key.lower()
            chosen_title = "Per-App IME Mode Persistence & Live Caret Indicator Demo"
            for lang_key, t in titles.items():
                if lang_key in k_lower:
                    chosen_title = t
                    break

            base_listing["trailers"] = [
                {
                    "videoFileName": "store-preview.mp4",
                    "imageFileName": "store-preview-thumb.png",
                    "title": chosen_title
                }
            ]
            listing["baseListing"] = base_listing

    print("Updating submission details via API...")
    updated_sub = api_request(f"{base_url}/submissions/{sub_id}", method="PUT", token=token, body=sub)
    print("Updated submission successfully!")

    # 6. Commit (publish) submission to Microsoft Store
    print("Committing submission to Microsoft Store...")
    commit_res = api_request(f"{base_url}/submissions/{sub_id}/commit", method="POST", token=token)
    print("Commit response:", commit_res)
    print("Successfully submitted 1080p trailers and MSIX to Microsoft Store via Ingestion API!")

if __name__ == "__main__":
    main()
