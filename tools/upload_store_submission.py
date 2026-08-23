import os
import sys
import json
import zipfile
import pathlib
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
    with urllib.request.urlopen(req) as resp:
        content = resp.read().decode("utf-8")
        return json.loads(content) if content else {}

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
        print(f"Blob upload finished with HTTP status: {resp.status}")

def main():
    tenant_id = os.environ["PARTNER_CENTER_TENANT_ID"]
    client_id = os.environ["PARTNER_CENTER_CLIENT_ID"]
    client_secret = os.environ["PARTNER_CENTER_CLIENT_SECRET"]
    video_path = pathlib.Path("packaging/store/store-preview.mp4")
    thumb_path = pathlib.Path("packaging/store/store-preview-thumb.png")
    app_id = "9P05QQZ2P5XC"

    print("Authenticating with Azure AD...")
    token = get_token(tenant_id, client_id, client_secret, "https://manage.devcenter.microsoft.com")
    base_url = f"https://manage.devcenter.microsoft.com/v1.0/my/applications/{app_id}"

    # 1. Clean up any existing pending submission
    print("Checking application status...")
    app_details = api_request(base_url, token=token)
    pending_sub = app_details.get("pendingApplicationSubmission")

    if not app_details.get("hasAdvancedListingPermission", False):
        print("WARNING: hasAdvancedListingPermission=false, trailers may not be supported for this app!")

    if pending_sub:
        pending_id = pending_sub["id"]
        print(f"Deleting leftover draft submission: {pending_id}...")
        try:
            urllib.request.urlopen(urllib.request.Request(
                f"{base_url}/submissions/{pending_id}",
                headers={"Authorization": f"Bearer {token}"},
                method="DELETE"
            ))
            print("Deleted leftover submission.")
        except Exception as e:
            print("Delete note:", e)

    # 2. Create a clean new draft submission
    print("Creating new draft submission...")
    sub = api_request(f"{base_url}/submissions", method="POST", token=token)
    sub_id = sub["id"]
    file_upload_url = sub["fileUploadUrl"]
    print(f"Created submission ID: {sub_id}")

    # Ensure video has an audio track (Microsoft Store requires audio channels on trailer videos)
    import subprocess
    import shutil
    if shutil.which("ffmpeg"):
        temp_audio_video = pathlib.Path("store-preview-audio.mp4")
        print("Ensuring video has an audio track via ffmpeg (adding silent stereo AAC if needed)...")
        subprocess.run([
            "ffmpeg", "-y",
            "-i", str(video_path),
            "-f", "lavfi", "-i", "anullsrc=channel_layout=stereo:sample_rate=44100",
            "-c:v", "copy",
            "-c:a", "aac",
            "-shortest",
            str(temp_audio_video)
        ], check=True)
        video_to_pack = temp_audio_video
    else:
        video_to_pack = video_path

    # 3. Pack 1080p video + thumbnail into zip WITH CORRECT DIRECTORY PATHS
    # Paths inside the zip must match the videoFileName / fileName values in the JSON
    zip_path = pathlib.Path("trailers_assets.zip")
    zip_contents = []
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.write(video_to_pack, arcname="Trailers/store-preview.mp4")
        zf.write(thumb_path, arcname="Images/store-preview-thumb.png")
        zip_contents = zf.namelist()
    print(f"Packed {zip_path.name} containing: {zip_contents}")

    # 4. Upload zip to Azure Blob Storage
    upload_zip_to_blob(file_upload_url, zip_path)

    # 5. Inject 5-language trailers at the TOP-LEVEL sub["trailers"] with correct trailerAssets structure
    titles = {
        "zh-tw": "即時輸入法模式維持與游標指示器動態演示",
        "zh-hant": "即時輸入法模式維持與游標指示器動態演示",
        "zh-cn": "实时输入法模式保持与光标指示器动态演示",
        "zh-hans": "实时输入法模式保持与光标指示器动态演示",
        "ja": "アプリごとのIME入力モード維持＆カーソルインジケーター実演",
        "ko": "앱별 IME 입력 모드 유지 및 커서 표시기 실시간 시연"
    }
    default_title = "Per-App IME Mode Persistence & Live Caret Indicator Demo"

    # Build trailerAssets keyed by locale, matching the listings returned by the API
    trailer_assets = {}
    if "listings" in sub:
        for key in sub["listings"].keys():
            k_lower = key.lower()
            chosen_title = default_title
            for lang_key, t in titles.items():
                if lang_key in k_lower:
                    chosen_title = t
                    break
            trailer_assets[k_lower] = {
                "title": chosen_title,
                "imageList": [
                    {
                        "fileName": "Images\\store-preview-thumb.png",
                        "description": "Trailer thumbnail"
                    }
                ]
            }

    # Fallback: ensure at least en-us is present
    if not trailer_assets:
        trailer_assets["en-us"] = {
            "title": default_title,
            "imageList": [
                {
                    "fileName": "Images\\store-preview-thumb.png",
                    "description": "Trailer thumbnail"
                }
            ]
        }

    # Set trailers at submission top-level (correct API location)
    sub["trailers"] = [
        {
            "videoFileName": "Trailers\\store-preview.mp4",
            "trailerAssets": trailer_assets
        }
    ]

    print(f"Updating submission with trailers for {len(trailer_assets)} locale(s): {list(trailer_assets.keys())}")
    updated_sub = api_request(f"{base_url}/submissions/{sub_id}", method="PUT", token=token, body=sub)
    print("Updated submission JSON successfully!")

    print("Committing submission to Microsoft Store...")
    commit_res = api_request(f"{base_url}/submissions/{sub_id}/commit", method="POST", token=token)
    print("Commit response:", commit_res)

    print("Polling live submission status from Microsoft Store...")
    import time
    max_retries = 30
    final_status = None
    for attempt in range(1, max_retries + 1):
        time.sleep(6)
        status_res = api_request(f"{base_url}/submissions/{sub_id}/status", token=token)
        curr_status = status_res.get("status")
        print(f"[{attempt}/{max_retries}] Current Submission Status: {curr_status}")
        
        errors = status_res.get("statusDetails", {}).get("errors", [])
        if errors:
            print("Errors detected:", json.dumps(errors, indent=2))
        
        if curr_status == "CommitFailed":
            raise RuntimeError(f"Submission commit failed with errors: {errors}")
        
        if curr_status not in ("CommitStarted", "PendingCommit"):
            final_status = curr_status
            print("Live Submission Status Details:", json.dumps(status_res, indent=2, ensure_ascii=False))
            break
    else:
        print("Note: Submission is still processing commit asynchronously.")

    current_sub = api_request(f"{base_url}/submissions/{sub_id}", token=token)
    print("Server Verified Trailers:")
    for trailer in current_sub.get("trailers", []):
        print(f"  videoFileName: {trailer.get('videoFileName')}")
        for locale, assets in trailer.get("trailerAssets", {}).items():
            img_count = len(assets.get("imageList", []))
            print(f"    [{locale}] title={assets.get('title')!r}, images={img_count}")

    print("=== Successfully verified 1080p Trailers on Microsoft Store via API! ===")

if __name__ == "__main__":
    main()
