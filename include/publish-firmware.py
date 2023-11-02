import requests
import sys
from os.path import basename

Import('env')

# from platformio import util
# project_config = util.load_project_config()
# bintray_config = {k: v for k, v in project_config.items("bintray")}
# version = project_config.get("common", "release_version")

try:
    import configparser
except ImportError:
    import ConfigParser as configparser
project_config = configparser.ConfigParser()
project_config.read("platformio.ini")
version = project_config.get("common", "release_version")
bintray_config = {k: v for k, v in project_config.items("publish")}

def publish_firmware(source, target, env):
    firmware_path = str(source[0])
    firmware_name = basename(firmware_path)
    print(firmware_name+" has been successfuly published!")


# def publish_firmware(source, target, env):
#     firmware_path = str(source[0])
#     firmware_name = basename(firmware_path)

#     print("Uploading {0} to Bintray. Version: {1}".format(
#         firmware_name, version))

#     url = "/".join([
#         "https://api.bintray.com", "content",
#         bintray_config.get("user"),
#         bintray_config.get("repository"),
#         bintray_config.get("package"), version, firmware_name
#     ])

#     headers = {
#         "Content-type": "application/octet-stream",
#         "X-Bintray-Publish": "1",
#         "X-Bintray-Override": "1"
#     }

#     r = None
#     try:
#         r = requests.put(url,
#                          data=open(firmware_path, "rb"),
#                          headers=headers,
#                          auth=(bintray_config.get("user"),
#                                bintray_config['api_token']))
#         r.raise_for_status()
#     except requests.exceptions.RequestException as e:
#         sys.stderr.write("Failed to submit package: %s\n" %
#                          ("%s\n%s" % (r.status_code, r.text) if r else str(e)))
#         env.Exit(1)

#     print("The firmware has been successfuly published at Bintray.com!")


# Custom upload command and program name
env.Replace(PROGNAME="firmware_v_%s" % version, UPLOADCMD=publish_firmware)