#include "Camera.h"
#include <windows.h>

#include "ErrorMap.h"

#include "Utils.h"
#include "Filesystem.h"
using namespace Filesystem;

#include <cstdio>
#include <cassert>
using namespace std;

const string Camera::c_cameraName_5D = "Canon EOS 5D Mark II";
const string Camera::c_cameraName_40D = "Canon EOS 40D";
const string Camera::c_cameraName_7D = "Canon EOS 7D";

const int Camera::LiveView::c_delay = 200;
const int Camera::LiveView::c_frameBufferSize = 0x800000;

const int Camera::c_sleepTimeout = 10000;
const int Camera::c_sleepAmount = 50;

bool Camera::s_initialized = false;
bool Camera::s_staticDataInitialized = false;

map<string, Camera::CameraModelData> Camera::s_modelData;
map<float, EdsUInt32> Camera::s_exposureCompensationValues;
map<EdsUInt32, float> Camera::s_exposureCompensationEnumToFloat;

stringstream * Camera::s_err = NULL;
queue<Camera::ErrorMessage> Camera::s_errMsgQueue;
Camera::ErrorLevel Camera::s_errorLevel = Camera::None;

void Camera::initialize()
{
    if (s_initialized)
        return;
    s_initialized = true;

    s_err = new stringstream;

    initStaticData();
    EdsInitializeSDK();
}

void Camera::initStaticData()
{
    if (s_staticDataInitialized)
        return;
    s_staticDataInitialized = true;

    // camera-specific information
    // 40D
    CameraModelData data40D;
    data40D.zoom100ImageSize.width = 1024;
    data40D.zoom100ImageSize.height = 680;
    data40D.zoom500ImageSize.width = 768;
    data40D.zoom500ImageSize.height = 800;
    data40D.zoom100MaxPosition.x = 3104;
    data40D.zoom100MaxPosition.y = 2016;
    data40D.zoom500MaxPosition.x = 3104;
    data40D.zoom500MaxPosition.y = 2080;
    data40D.zoomBoxSize.width = 204;
    data40D.zoomBoxSize.height = 208;

    // 5D
    CameraModelData data5D;
    data5D.zoom100ImageSize.width = 1024;
    data5D.zoom100ImageSize.height = 680;
    data5D.zoom500ImageSize.width = 1120;
    data5D.zoom500ImageSize.height = 752;
    data5D.zoom100MaxPosition.x = 4464;
    data5D.zoom100MaxPosition.y = 2976;
    data5D.zoom500MaxPosition.x = 4464;
    data5D.zoom500MaxPosition.y = 2976;
    data5D.zoomBoxSize.width = 202;
    data5D.zoomBoxSize.height = 135;

    // 7D
    CameraModelData data7D;
    data7D.zoom100ImageSize.width = 1056;
    data7D.zoom100ImageSize.height = 704;
    data7D.zoom500ImageSize.width = 1024;
    data7D.zoom500ImageSize.height = 680;
    data7D.zoom100MaxPosition.x = 4136;
    data7D.zoom100MaxPosition.y = 2754;
    data7D.zoom500MaxPosition.x = 4136;
    data7D.zoom500MaxPosition.y = 2754;
    data7D.zoomBoxSize.width = 212;
    data7D.zoomBoxSize.height = 144;

    s_modelData[c_cameraName_40D] = data40D;
    s_modelData[c_cameraName_5D] = data5D;
    s_modelData[c_cameraName_7D] = data7D;

    s_exposureCompensationValues[3.0f] =              0x18;
    s_exposureCompensationValues[2.0f+2.0f/3.0f] =    0x15;
    s_exposureCompensationValues[2.0f+1.0f/2.0f] =    0x14;
    s_exposureCompensationValues[2.0f+1.0f/3.0f] =    0x13;
    s_exposureCompensationValues[2.0f] =              0x10;
    s_exposureCompensationValues[1.0f+2.0f/3.0f] =    0x0D;
    s_exposureCompensationValues[1.0f+1.0f/2.0f] =    0x0C;
    s_exposureCompensationValues[1.0f+1.0f/3.0f] =    0x0B;
    s_exposureCompensationValues[1.0f] =              0x08;
    s_exposureCompensationValues[2.0f/3.0f] =         0x05;
    s_exposureCompensationValues[1.0f/2.0f] =         0x04;
    s_exposureCompensationValues[1.0f/3.0f] =         0x03;
    s_exposureCompensationValues[0.0f] =              0x00;
    s_exposureCompensationValues[-1.0f/3.0f] =        0xFD;
    s_exposureCompensationValues[-1.0f/2.0f] =        0xFC;
    s_exposureCompensationValues[-2.0f/3.0f] =        0xFB;
    s_exposureCompensationValues[-1.0f] =             0xF8;
    s_exposureCompensationValues[-1.0f-1.0f/3.0f] =   0xF5;
    s_exposureCompensationValues[-1.0f-1.0f/2.0f] =   0xF4;
    s_exposureCompensationValues[-1.0f-2.0f/3.0f] =   0xF3;
    s_exposureCompensationValues[-2.0f] =             0xF0;
    s_exposureCompensationValues[-2.0f-1.0f/3.0f] =   0xED;
    s_exposureCompensationValues[-2.0f-1.0f/2.0f] =   0xEC;
    s_exposureCompensationValues[-2.0f-2.0f/3.0f] =   0xEB;
    s_exposureCompensationValues[-3.0f] =             0xE8;

    for (map<float, EdsUInt32>::iterator it = s_exposureCompensationValues.begin(); it != s_exposureCompensationValues.end(); it++)
        s_exposureCompensationEnumToFloat[it->second] = it->first;
}

Camera::LiveView::LiveView() :
    m_state(Off),
    m_streamPtr(NULL)
{
    // set up buffer
    m_frameBuffer = new unsigned char[c_frameBufferSize];
    EdsError err = EdsCreateMemoryStreamFromPointer(m_frameBuffer, c_frameBufferSize, &m_streamPtr);
    
    if (err) {
        (*Camera::s_err) << "Unable to create memory stream for live view: " << ErrorMap::errorMsg(err);
        Camera::pushErrMsg(Camera::Error);
    }
}

Camera::LiveView::~LiveView()
{
    EdsRelease(m_streamPtr);
    delete[] m_frameBuffer;
}

Camera::Camera() :
    m_liveView(new LiveView()),
    m_pendingZoomPosition(false),
    m_zoomRatio(1),
    m_pendingZoomRatio(false),
    m_whiteBalance(kEdsWhiteBalance_Auto),
    m_pendingWhiteBalance(false),
    m_pictureCompleteCallback(NULL),
    m_connected(false),
    m_cameraData(NULL)
{
    m_zoomPosition.x = 0;
    m_zoomPosition.y = 0;
    m_pendingZoomPoint.x = 0;
    m_pendingZoomPoint.y = 0;
}

Camera::~Camera()
{
    disconnect();
    delete m_liveView;
}

bool Camera::disconnect()
{
    if (m_connected) {
        // release session
        EdsError err;
        err = EdsCloseSession(m_cam);

        bool success = true;
        if (err) {
            *s_err << "Unable to close session: " << ErrorMap::errorMsg(err);
            pushErrMsg();
            success = false;
        }

        err = EdsRelease(m_cam);

        if (err) {
            *s_err << "Unable to deallocate session: " << ErrorMap::errorMsg(err);
            pushErrMsg(Warning);
        }

        return success;
    }

    return true;
}

Camera * Camera::getFirstCamera()
{
    initialize();

    Camera * cam = new Camera();

    EdsCameraListRef camList = NULL;
    EdsError err;

    err = EdsGetCameraList(&camList);
    if (err) {
        *(cam->s_err) << "Unable to get camera list: " << ErrorMap::errorMsg(err);
        cam->pushErrMsg();
        delete cam;
        return NULL;
    }
    if (! camList) {
        *(cam->s_err) << "EDSDK didn't give us a camera list ref.";
        cam->pushErrMsg();
        delete cam;
        return NULL;
    }

    EdsUInt32 camCount = 0;
    err = EdsGetChildCount(camList, &camCount);

    if (err) {
        *(cam->s_err) << "Unable to get camera count: " << ErrorMap::errorMsg(err);
        cam->pushErrMsg();
        EdsRelease(camList);
        delete cam;
        return NULL;
    }

    if (camCount == 0) {
        *(cam->s_err) << "No camera connected.";
        cam->pushErrMsg(Warning);
        EdsRelease(camList);
        delete cam;
        return NULL;
    }

    // get the first camera
    EdsCameraRef camHandle;
    err = EdsGetChildAtIndex(camList, 0, &camHandle);

    if (err) {
        *(cam->s_err) << "Unable to get connected camera handle: " << ErrorMap::errorMsg(err);
        cam->pushErrMsg(Error);
        EdsRelease(camList);
        delete cam;
        return NULL;
    }

    cam->m_cam = camHandle;

    err = EdsRelease(camList);

    if (err) {
        *(cam->s_err) << "Unable to release camera list handle: " << ErrorMap::errorMsg(err);
        cam->pushErrMsg(Warning);
    }

    return cam;
}

const Camera::CameraModelData * Camera::cameraSpecificData() const
{
    return m_cameraData;
}

bool Camera::connect()
{
    EdsError err;

    // open a session
    err = EdsOpenSession(m_cam);

    if (err) {
        *s_err << "Unable to open session: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        return false;
    }

    // handlers
    err = EdsSetCameraStateEventHandler(m_cam, kEdsStateEvent_All, &staticStateEventHandler, this);

    if (err) {
        *s_err << "Unable to set camera state event handler: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        return false;
    }

    err = EdsSetObjectEventHandler(m_cam, kEdsObjectEvent_All, &staticObjectEventHandler, this);

    if (err) {
        *s_err << "Unable to set object event handler: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        return false;
    }
    
    err = EdsSetPropertyEventHandler(m_cam, kEdsPropertyEvent_All, &staticPropertyEventHandler, this);

    if (err) {
        *s_err << "Unable to set property event handler: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        return false;
    }

    // set default options
    // save to computer, not memory card
    EdsUInt32 value = kEdsSaveTo_Host;
    err = EdsSetPropertyData(m_cam, kEdsPropID_SaveTo, 0, sizeof(EdsUInt32), &value);

    if (err) {
        *s_err << "Unable to set property SaveTo device to computer: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        return false;
    }

    m_name = getName();

    if (s_modelData.count(m_name) > 0) {
        m_cameraData = &s_modelData[m_name];
    } else {
        *s_err << "Unrecognized camera model: " << m_name;
        pushErrMsg(Warning);
        // default to 7D
        return &s_modelData[c_cameraName_7D];
    }

    return true;
}

EdsError EDSCALLBACK Camera::staticObjectEventHandler(EdsObjectEvent inEvent, EdsBaseRef inRef, EdsVoid * inContext)
{
    // transfer from static to member
    if (! inContext)
        return 0;

    ((Camera *) inContext)->objectEventHandler(inEvent, inRef);

    if (inRef)
        EdsRelease(inRef);

    return 0;
}

EdsError EDSCALLBACK Camera::staticStateEventHandler(EdsStateEvent inEvent, EdsUInt32 inEventData, EdsVoid * inContext)
{
    // transfer from static to member
    if (! inContext)
        return 0;

    ((Camera *) inContext)->stateEventHandler(inEvent, inEventData);

    return 0;
}

EdsError EDSCALLBACK Camera::staticPropertyEventHandler(EdsPropertyEvent inEvent, EdsPropertyID inPropertyID, EdsUInt32 inParam, EdsVoid * inContext)
{
    // transfer from static to member
    if (! inContext)
        return 0;

    ((Camera *) inContext)->propertyEventHandler(inEvent, inPropertyID, inParam);

    return 0;
}

void Camera::objectEventHandler(EdsObjectEvent inEvent, EdsBaseRef inRef)
{
    if (inEvent == kEdsObjectEvent_DirItemRequestTransfer) {
        transferOneItem(inRef, m_picOutFile);
    } else {
        *s_err << "objectEventHandler: event " << inEvent;
        pushErrMsg(Debug);
    }
}

void Camera::stateEventHandler(EdsStateEvent inEvent, EdsUInt32 inEventData)
{
    *s_err << "stateEventHandler: event " << inEvent << ", parameter " << inEventData;
    pushErrMsg(Debug);
}

void Camera::propertyEventHandler(EdsPropertyEvent inEvent, EdsPropertyID inPropertyID, EdsUInt32 inParam)
{
    switch (inPropertyID) {
		case kEdsPropID_Unknown:
            *s_err << "Incoming property event: Unknown";
            pushErrMsg(Warning);
			break;
		case kEdsPropID_ProductName:
            *s_err << "Incoming property event: ProductName";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_BodyID:
            *s_err << "Incoming property event: BodyID";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_OwnerName:
            *s_err << "Incoming property event: OwnerName";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_MakerName:
            *s_err << "Incoming property event: MakerName";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_DateTime:
            *s_err << "Incoming property event: DateTime";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_FirmwareVersion:
            *s_err << "Incoming property event: FirmwareVersion";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_BatteryLevel:
            *s_err << "Incoming property event: BatteryLevel";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_CFn:
            *s_err << "Incoming property event: CFn";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_SaveTo:
            *s_err << "Incoming property event: SaveTo";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_CurrentStorage:
            *s_err << "Incoming property event: CurrentStorage";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_CurrentFolder:
            *s_err << "Incoming property event: CurrentFolder";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_MyMenu:
            *s_err << "Incoming property event: MyMenu";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_BatteryQuality:
            *s_err << "Incoming property event: BatteryQuality";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_HDDirectoryStructure:
            *s_err << "Incoming property event: HDDirectoryStructure";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_ImageQuality:
            *s_err << "Incoming property event: ImageQuality";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_JpegQuality:
            *s_err << "Incoming property event: JpegQuality";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Orientation:
            *s_err << "Incoming property event: Orientation";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_ICCProfile:
            *s_err << "Incoming property event: ICCProfile";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_FocusInfo:
            *s_err << "Incoming property event: FocusInfo";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_DigitalExposure:
            *s_err << "Incoming property event: DigitalExposure";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_WhiteBalance:
            *s_err << "Incoming property event: WhiteBalance";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_ColorTemperature:
            *s_err << "Incoming property event: ColorTemperature";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_WhiteBalanceShift:
            *s_err << "Incoming property event: WhiteBalanceShift";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Contrast:
            *s_err << "Incoming property event: Contrast";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_ColorSaturation:
            *s_err << "Incoming property event: ColorSaturation";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_ColorTone:
            *s_err << "Incoming property event: ColorTone";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Sharpness:
            *s_err << "Incoming property event: Sharpness";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_ColorSpace:
            *s_err << "Incoming property event: ColorSpace";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_ToneCurve:
            *s_err << "Incoming property event: ToneCurve";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_PhotoEffect:
            *s_err << "Incoming property event: PhotoEffect";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_FilterEffect:
            *s_err << "Incoming property event: FilterEffect";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_ToningEffect:
            *s_err << "Incoming property event: ToningEffect";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_ParameterSet:
            *s_err << "Incoming property event: ParameterSet";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_ColorMatrix:
            *s_err << "Incoming property event: ColorMatrix";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_PictureStyle:
            *s_err << "Incoming property event: PictureStyle";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_PictureStyleDesc:
            *s_err << "Incoming property event: PictureStyleDesc";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_ETTL2Mode:
            *s_err << "Incoming property event: ETTL2Mode";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_PictureStyleCaption:
            *s_err << "Incoming property event: PictureStyleCaption";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Linear:
            *s_err << "Incoming property event: Linear";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_ClickWBPoint:
            *s_err << "Incoming property event: ClickWBPoint";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_WBCoeffs:
            *s_err << "Incoming property event: WBCoeffs";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_GPSVersionID:
            *s_err << "Incoming property event: GPSVersionID";
            pushErrMsg(Debug);
			break;
		case    kEdsPropID_GPSLatitudeRef:
            *s_err << "Incoming property event: GPSLatitudeRef";
            pushErrMsg(Debug);
			break;
		case    kEdsPropID_GPSLatitude:
            *s_err << "Incoming property event: GPSLatitude";
            pushErrMsg(Debug);
			break;
		case    kEdsPropID_GPSLongitudeRef:
            *s_err << "Incoming property event: GPSLongitudeRef";
            pushErrMsg(Debug);
			break;
		case    kEdsPropID_GPSLongitude:
            *s_err << "Incoming property event: GPSLongitude";
            pushErrMsg(Debug);
			break;
		case    kEdsPropID_GPSAltitudeRef:
            *s_err << "Incoming property event: GPSAltitudeRef";
            pushErrMsg(Debug);
			break;
		case    kEdsPropID_GPSAltitude:
            *s_err << "Incoming property event: GPSAltitude";
            pushErrMsg(Debug);
			break;
		case    kEdsPropID_GPSTimeStamp:
            *s_err << "Incoming property event: GPSTimeStamp";
            pushErrMsg(Debug);
			break;
		case    kEdsPropID_GPSSatellites:
            *s_err << "Incoming property event: GPSSatellites";
            pushErrMsg(Debug);
			break;
		case    kEdsPropID_GPSStatus:
            *s_err << "Incoming property event: GPSStatus";
            pushErrMsg(Debug);
			break;
		case    kEdsPropID_GPSMapDatum:
            *s_err << "Incoming property event: GPSMapDatum";
            pushErrMsg(Debug);
			break;
		case    kEdsPropID_GPSDateStamp:
            *s_err << "Incoming property event: GPSDateStamp";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_AtCapture_Flag:
            *s_err << "Incoming property event: AtCapture_Flag";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_AEMode:
            *s_err << "Incoming property event: AEMode";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_DriveMode:
            *s_err << "Incoming property event: DriveMode";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_ISOSpeed:
            *s_err << "Incoming property event: ISOSpeed";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_MeteringMode:
            *s_err << "Incoming property event: MeteringMode";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_AFMode:
            *s_err << "Incoming property event: AFMode";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Av:
            *s_err << "Incoming property event: Av";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Tv:
            *s_err << "Incoming property event: Tv";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_ExposureCompensation:
            *s_err << "Incoming property event: ExposureCompensation";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_FlashCompensation:
            *s_err << "Incoming property event: FlashCompensation";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_FocalLength:
            *s_err << "Incoming property event: FocalLength";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_AvailableShots:
            *s_err << "Incoming property event: AvailableShots";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Bracket:
            *s_err << "Incoming property event: Bracket";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_WhiteBalanceBracket:
            *s_err << "Incoming property event: WhiteBalancingBracket";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_LensName:
            *s_err << "Incoming property event: LensName";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_AEBracket:
            *s_err << "Incoming property event: AEBracket";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_FEBracket:
            *s_err << "Incoming property event: FEBracket";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_ISOBracket:
            *s_err << "Incoming property event: ISOBracket";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_NoiseReduction:
            *s_err << "Incoming property event: NoiseReduction";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_FlashOn:
            *s_err << "Incoming property event: FlashOn";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_RedEye:
            *s_err << "Incoming property event: RedEye";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_FlashMode:
            *s_err << "Incoming property event: FlashMode";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_LensStatus:
            *s_err << "Incoming property event: LensStatus";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Artist:
            *s_err << "Incoming property event: Artist";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Copyright:
            *s_err << "Incoming property event: Copyright";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_DepthOfField:
            *s_err << "Incoming property event: DepthOfField";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_EFCompensation:
            *s_err << "Incoming property event: EFCompensation";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Evf_OutputDevice:
            // we have experimentally determined that the inParam for this
            // property is completely bogus. EDSDK 2.7
            if (inParam == kEdsEvfOutputDevice_PC) {
                *s_err << "bogus notice from camera: output device PC (live mode).";
                pushErrMsg(Debug);
            } else if (inParam == kEdsEvfOutputDevice_TFT) {
                *s_err << "bogus notice from camera: output device TFT (live mode).";
                pushErrMsg(Warning);
            } else if (inParam == 0) {
                *s_err << "bogus notice from camera: we are no longer in live view mode.";
                pushErrMsg(Debug);
            } else {
                // should not get here
                *s_err << "bogus notice from camera: live view now in WTF mode: " << inParam;
                pushErrMsg(Warning);
            }
            handleCameraIsReady();
            break;
		case kEdsPropID_Evf_Mode:
            *s_err << "Incoming property event: EvfMode";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Evf_WhiteBalance:
            *s_err << "Incoming property event: WhiteBalance";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Evf_ColorTemperature:
            *s_err << "Incoming property event: ColorTemperature";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Evf_DepthOfFieldPreview:
            *s_err << "Incoming property event: DepthOfFieldPreview";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Evf_Zoom:
            *s_err << "Incoming property event: Zoom";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Evf_ZoomPosition:
            *s_err << "Incoming property event: ZoomPosition";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Evf_FocusAid:
            *s_err << "Incoming property event: FocusAid";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Evf_Histogram:
            *s_err << "Incoming property event: Histogram";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Evf_ImagePosition:
            *s_err << "Incoming property event: ImagePosition";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Evf_HistogramStatus:
            *s_err << "Incoming property event: HistogramStatus";
            pushErrMsg(Debug);
			break;
		case kEdsPropID_Evf_AFMode:
            *s_err << "Incoming property event: AFMode";
            pushErrMsg(Debug);
			break;
        default:
            *s_err << "Unrecognized prop id: " << inPropertyID;
            pushErrMsg(Warning);
            assert(false);
    }
}

bool Camera::transferOneItem(EdsBaseRef inRef, string outfile)
{
    // transfer the image in memory to disk
    EdsDirectoryItemInfo dirItemInfo;
    EdsStreamRef outStream = NULL;

    EdsError err;

    err = EdsGetDirectoryItemInfo(inRef, &dirItemInfo);

    if (err) {
        *s_err << "Unable to get directory item info: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        return false;
    }

    // get a temp file to write to
    string tmpfile = tmpnam(NULL);

    // this creates the outStream that is used by EdsDownload to actually
    // grab and write out the file
    err = EdsCreateFileStream(tmpfile.c_str(), kEdsFileCreateDisposition_CreateAlways, kEdsAccess_ReadWrite, &outStream);

    if (err) {
        *s_err << "Unable to create file stream: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        return false;
    }

    if (! outStream) {
        *s_err << "Create file stream didn't allocate a stream for us.";
        pushErrMsg();
        return false;
    }

    // do the transfer
    err = EdsDownload(inRef, dirItemInfo.size, outStream);
 
    if (err) {
        *s_err << "Unable to download picture: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        EdsRelease(outStream);
        return false;
    }

    err = EdsDownloadComplete(inRef);

    if (err) {
        *s_err << "Unable to finish downloading picture: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        EdsRelease(outStream);
        return false;
    }

    // clean up
    err = EdsRelease(outStream);

    if (err) {
        *s_err << "Unable to release out stream after downloading: " << ErrorMap::errorMsg(err);
        pushErrMsg(Warning);
    }

    // make sure we don't overwrite files
    ensurePathExists(getDirectoryName(outfile));
    outfile = makeUnique(outfile);
    moveFile(tmpfile, outfile);

    resumeLiveView();
    handleCameraIsReady();

    if (m_pictureCompleteCallback)
        m_pictureCompleteCallback(outfile);

    m_pictureDoneQueue.push(outfile);

    return true;
}

void Camera::handleCameraIsReady()
{
    switch (m_liveView->m_state) {
        case LiveView::On:
        case LiveView::Off:
        case LiveView::Paused:
            break;
        case LiveView::WaitingToStart:
            m_liveView->m_state = LiveView::On;
            switch (m_liveView->m_desiredNewState) {
                case LiveView::Off:
                    stopLiveView();
                    break;
                case LiveView::On:
                    break;
                case LiveView::Paused:
                    pauseLiveView();
                    break;
                default:
                    assert(false);
            }
            break;
        case LiveView::WaitingToStop:
            *s_err << "Finished stopping live view. Let's decide what to do.";
            pushErrMsg(Debug);
            m_liveView->m_state = LiveView::Off;
            switch (m_liveView->m_desiredNewState) {
                case LiveView::Off:
                    *s_err << "we want it off, so we don't have to do anything.";
                    pushErrMsg(Debug);
                    break;
                case LiveView::On:
                    *s_err << "we want it on, so we call startLiveView";
                    pushErrMsg(Debug);
                    startLiveView();
                    break;
                case LiveView::Paused:
                    *s_err << "we want it paused, so we set state to paused";
                    pushErrMsg(Debug);
                    m_liveView->m_state = LiveView::Paused;
                    break;
                default:
                    assert(false);
            }
            break;
    }
}

bool Camera::pauseLiveView()
{
    switch (m_liveView->m_state) {
        case LiveView::Paused:
        case LiveView::Off:
            // nothing to do
            return true;
        case LiveView::On:
            if (_stopLiveView()) {
                m_liveView->m_desiredNewState = LiveView::Paused;
                return true;
            } else {
                return false;
            }
        case LiveView::WaitingToStart:
        case LiveView::WaitingToStop:
            m_liveView->m_desiredNewState = LiveView::Paused;
            return true;
    }
    assert(false);
    return false;
}

bool Camera::resumeLiveView()
{
    *s_err << "Attempting to resume live view.";
    pushErrMsg(Debug);
    switch (m_liveView->m_state) {
        case LiveView::Off:
            *s_err << "Live view is off, we don't need to resume.";
            pushErrMsg(Debug);
            return true;
        case LiveView::On:
            *s_err << "Live view already on, don't need to do anything.";
            pushErrMsg(Debug);
            return true;
        case LiveView::Paused:
            // this is the normal way to resume
            *s_err << "live view is paused, so we call _startLiveView";
            pushErrMsg(Debug);
            if (! _startLiveView())
                return false;
            m_liveView->m_desiredNewState = LiveView::On;
        case LiveView::WaitingToStop:
            *s_err << "Already waiting to stop live view, so we change the next desired state to On";
            pushErrMsg(Debug);
            m_liveView->m_desiredNewState = LiveView::On;
            return true;
        case LiveView::WaitingToStart:
            *s_err << "Already waiting to start live view, so we change the next desired state to On";
            pushErrMsg(Debug);
            m_liveView->m_desiredNewState = LiveView::On;
            return true;
    }
    assert(false);
    return false;
}

bool Camera::setComputerCapabilities()
{
    // tell the camera how much disk space we have left
    EdsCapacity caps;

    caps.reset = true;
    caps.bytesPerSector = 512;
    caps.numberOfFreeClusters = 2147483647; // arbitrary large number
    EdsError err = EdsSetCapacity(m_cam, caps);

    if (err) {
        *s_err << "Unable to set computer capabilities: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        return false;
    }

    return true;
}

bool Camera::takeSinglePicture(string outFile)
{
    if (! pauseLiveView())
        return false;
    m_picOutFile = outFile;

    if (! setComputerCapabilities())
        return false;

    *s_err << "Sending take picture command.";
    pushErrMsg(Debug);
    // take a picture with the camera and save it to outfile
    EdsError err = EdsSendCommand(m_cam, kEdsCameraCommand_TakePicture, 0);

    if (err == EDS_ERR_OBJECT_NOTREADY) {
        *s_err << "unable to take picture, camera not ready";
        pushErrMsg(Warning);
        return false;
    } else if (err) {
        *s_err << "unable to take picture: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        return false;
    }

    return true;
}

EdsPoint Camera::zoomPosition() const
{
    return m_zoomPosition;
}

void Camera::setZoomPosition(EdsPoint position)
{
    m_pendingZoomPoint = position;
    m_pendingZoomPosition = true;
}

int Camera::zoomRatio() const
{
    return m_zoomRatio;
}

void Camera::setZoomRatio(int zoomRatio)
{
    m_zoomRatio = zoomRatio;
    m_pendingZoomRatio = true;
}

EdsWhiteBalance Camera::whiteBalance() const
{
    EdsError err = EdsGetPropertyData(m_cam, kEdsPropID_WhiteBalance, 0, sizeof(m_whiteBalance), (EdsVoid *) &m_whiteBalance);
    if (err) {
        *s_err << "Unable to get white balance: " << ErrorMap::errorMsg(err);
        pushErrMsg();
    }
    return m_whiteBalance;
}

void Camera::setWhiteBalance(EdsWhiteBalance whiteBalance) 
{
    m_whiteBalance = whiteBalance;
    m_pendingWhiteBalance = true;
}

Camera::MeteringMode Camera::meteringMode() const
{
    EdsUInt32 mode;
    EdsError err = EdsGetPropertyData(m_cam, kEdsPropID_MeteringMode, 0, sizeof(EdsUInt32), (EdsVoid *) &mode);
    if (err) {
        *s_err << "Unable to get metering mode: " << ErrorMap::errorMsg(err);
        pushErrMsg();
    }
    return (MeteringMode) mode;
}

void Camera::setMeteringMode(MeteringMode mode)
{
    EdsUInt32 edsMode = mode;
    EdsError err = EdsSetPropertyData(m_cam, kEdsPropID_MeteringMode, 0, sizeof(EdsUInt32), &edsMode);
    if (err) {
        *s_err << "Unable to set metering mode: " << ErrorMap::errorMsg(err);
        pushErrMsg(Warning);
    }
}

Camera::DriveMode Camera::driveMode() const
{
    EdsUInt32 mode;
    EdsError err = EdsGetPropertyData(m_cam, kEdsPropID_DriveMode, 0, sizeof(EdsUInt32), (EdsVoid *) &mode);
    if (err) {
        *s_err << "Unable to get drive mode: " << ErrorMap::errorMsg(err);
        pushErrMsg();
    }
    return (DriveMode) mode;
}

void Camera::setDriveMode(DriveMode mode)
{
    EdsUInt32 edsMode = mode;
    EdsError err = EdsSetPropertyData(m_cam, kEdsPropID_DriveMode, 0, sizeof(EdsUInt32), &edsMode);
    if (err) {
        *s_err << "Unable to set drive mode: " << ErrorMap::errorMsg(err);
        pushErrMsg(Warning);
    }
}

Camera::AFMode Camera::afMode() const
{
    EdsUInt32 mode;
    EdsError err = EdsGetPropertyData(m_cam, kEdsPropID_AFMode, 0, sizeof(EdsUInt32), (EdsVoid *) &mode);
    if (err) {
        *s_err << "Unable to get AF mode: " << ErrorMap::errorMsg(err);
        pushErrMsg();
    }
    return (AFMode) mode;
}

void Camera::setAFMode(AFMode mode)
{
    EdsUInt32 edsMode = mode;
    EdsError err = EdsSetPropertyData(m_cam, kEdsPropID_AFMode, 0, sizeof(EdsUInt32), &edsMode);
    if (err) {
        *s_err << "Unable to set AF mode: " << ErrorMap::errorMsg(err);
        pushErrMsg(Warning);
    }
}

float Camera::exposureCompensation() const
{
    EdsUInt32 value;
    EdsError err = EdsGetPropertyData(m_cam, kEdsPropID_ExposureCompensation, 0, sizeof(EdsUInt32), &value);
    if (err) {
        *s_err << "Unable to get exposure compensation: " << ErrorMap::errorMsg(err);
        pushErrMsg();
    }

    return Utils::value(s_exposureCompensationEnumToFloat, value, 0.0f);
}

void Camera::setExposureCompensation(float value)
{
    EdsError err;
    err = EdsSendCommand(m_cam, (EdsUInt32)kEdsCameraCommand_DoEvfAf, (EdsUInt32)kEdsCameraCommand_EvfAf_OFF);
    if (err) {
        *s_err << "Unable to turn live view auto focus off: " << ErrorMap::errorMsg(err);
        pushErrMsg();
    }

    EdsUInt32 enumValue = Utils::closest(s_exposureCompensationValues, value);
    err = EdsSetPropertyData(m_cam, kEdsPropID_ExposureCompensation, 0, sizeof(EdsUInt32), &enumValue);
    if (err) {
        *s_err << "Unable to set exposure compensation: " << ErrorMap::errorMsg(err);
        pushErrMsg(Warning);
    }

}

string Camera::getName() const
{
    EdsDeviceInfo deviceInfo;
    EdsError err = EdsGetDeviceInfo(m_cam, &deviceInfo);

    if (err) {
        *s_err << "Unable to get device info: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        return string();
    }

    return deviceInfo.szDeviceDescription;
}

void Camera::setPictureCompleteCallback(takePictureCompleteCallback callback)
{
    m_pictureCompleteCallback = callback;
}

int Camera::pictureDoneQueueSize() const
{
    return m_pictureDoneQueue.size();
}

string Camera::popPictureDoneQueue()
{
    if (pictureDoneQueueSize() == 0) {
        return string();
    } else {
        string value = m_pictureDoneQueue.front();
        m_pictureDoneQueue.pop();
        return value;
    }
}

const unsigned char * Camera::liveViewFrameBuffer() const
{
    return m_liveView->m_frameBuffer;
}

int Camera::liveViewFrameBufferSize() const
{
    return m_liveView->c_frameBufferSize;
}

bool Camera::grabLiveViewFrame()
{
    // skip frames if the camera isn't ready yet.
    if (m_liveView->m_state != LiveView::On) {
        *s_err << "Skipping live view frame because camera is not in live view mode";
        if (m_liveView->m_state == LiveView::WaitingToStart)
            *s_err << " yet";
        pushErrMsg(Warning);
        return false;
    }

    EdsError err;
    EdsImageRef img = NULL;

    // create image
    err = EdsCreateEvfImageRef(m_liveView->m_streamPtr, &img);
    if (err) {
        *s_err << "Unable to create live view frame on the camera: " << ErrorMap::errorMsg(err);
        pushErrMsg(Error);
        return false;
    }
    if (! img) {
        *s_err << "EDSDK didn't give us an image ref for live view frame";
        pushErrMsg(Error);
        return false;
    }

    // download the frame
    err = EdsDownloadEvfImage(m_cam, img);

    if (err == EDS_ERR_OBJECT_NOTREADY) {
        // skip the frame if the camera isn't ready
        *s_err << "skipping live view frame because camera isn't ready";
        pushErrMsg(Warning);
        EdsRelease(img);
        return false;
    } else if (err) {
        // skip the frame. unknown error
        *s_err << "skipping live view frame: " << ErrorMap::errorMsg(err);
        pushErrMsg(Error);
        EdsRelease(img);
        return false;
    }

    // get/set zoom ratio
    if (m_pendingZoomRatio) {
        err = EdsSetPropertyData(m_cam, kEdsPropID_Evf_Zoom, 0, sizeof(EdsUInt32), &m_zoomRatio);
        if (err) {
            *s_err << "Unable to set zoom ratio: " << ErrorMap::errorMsg(err);
            pushErrMsg(Warning);
        } else {
            m_pendingZoomRatio = false;
        }
    } else {
        err = EdsGetPropertyData(img, kEdsPropID_Evf_Zoom, 0, sizeof(EdsUInt32), &m_zoomRatio);
        if (err) {
            *s_err << "Unable to get zoom ratio: " << ErrorMap::errorMsg(err);
            pushErrMsg(Warning);
        }
    }

    // get/set zoom position
    if (m_pendingZoomPosition) {
        err = EdsSetPropertyData(m_cam, kEdsPropID_Evf_ZoomPosition, 0, sizeof(EdsPoint), &m_pendingZoomPoint);
        if (err) {
            *s_err << "Unable to set zoom position: " << ErrorMap::errorMsg(err);
            pushErrMsg(Warning);
        } else {
            m_pendingZoomPosition = false;
        }
    } else {
        err = EdsGetPropertyData(img, kEdsPropID_Evf_ZoomPosition, 0, sizeof(EdsPoint), &m_zoomPosition);
        if (err) {
            *s_err << "Unable to get zoom position: " << ErrorMap::errorMsg(err);
            pushErrMsg(Warning);
        }
    }

    // set white balance
    if (m_pendingWhiteBalance) {
        err = EdsSetPropertyData(m_cam, kEdsPropID_Evf_WhiteBalance, 0, sizeof(EdsWhiteBalance), &m_whiteBalance);
        if (err) {
            *s_err << "Unable to set white balance: " << ErrorMap::errorMsg(err);
            pushErrMsg(Warning);
        } else {
            m_pendingWhiteBalance = false;
        }
    }

    err = EdsRelease(img);
    if (err) {
        *s_err << "Unable to release live view frame: " << ErrorMap::errorMsg(err);
        pushErrMsg(Warning);
    }

    return true;
}

bool Camera::startLiveView()
{
    switch (m_liveView->m_state) {
        case LiveView::Paused:
            *s_err << "startLiveView(): Live view paused, will resume in a moment.";
            pushErrMsg(Warning);
            return true;
        case LiveView::Off:
            if (! _startLiveView())
                return false;
            m_liveView->m_desiredNewState = LiveView::On;
            return true;
        case LiveView::WaitingToStop:
            *s_err << "Waiting for live view to end so we can start it again.";
            pushErrMsg(Debug);
            m_liveView->m_desiredNewState = LiveView::On;
            return true;
        case LiveView::WaitingToStart:
            *s_err << "startLiveView(): already waiting to start live view.";
            pushErrMsg(Warning);
            m_liveView->m_desiredNewState = LiveView::On;
            return true;
        case LiveView::On:
            *s_err << "startLiveView(): Live view already on";
            pushErrMsg(Warning);
            return true;
    }
    assert(false);
    return false;
}

bool Camera::_startLiveView()
{
    // tell the computer to send live data to the computer
    EdsError err;
    EdsUInt32 device;
    err = EdsGetPropertyData(m_cam, kEdsPropID_Evf_OutputDevice, 0, sizeof(EdsUInt32), &device);

    if (err) {
        *s_err << "Unable to get live view output device: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        return false;
    }

    device |= kEdsEvfOutputDevice_PC;
    err = EdsSetPropertyData(m_cam, kEdsPropID_Evf_OutputDevice, 0, sizeof(EdsUInt32), &device);

    if (err) {
        *s_err << "Unable to turn on live view: " << ErrorMap::errorMsg(err);
        pushErrMsg();
    }

    *s_err << "Requested live view to turn on.";
    pushErrMsg(Debug);
    m_liveView->m_state = LiveView::WaitingToStart;

    return true;
}

bool Camera::stopLiveView()
{
    switch (m_liveView->m_state) {
        case LiveView::Paused:
            *s_err << "stopLiveView(): Live view already paused";
            pushErrMsg(Debug);
            m_liveView->m_state = LiveView::Off;
            return true;
        case LiveView::Off:
            *s_err << "stopLiveView(): Live view already off";
            pushErrMsg(Debug);
            return true;
        case LiveView::WaitingToStop:
        case LiveView::WaitingToStart:
            m_liveView->m_desiredNewState = LiveView::Off;
            return true;
        case LiveView::On:
            if (! _stopLiveView())
                return false;
            m_liveView->m_desiredNewState = LiveView::Off;
            return true;
    }
    assert(false);
    return false;
}

bool Camera::_stopLiveView()
{
    // tell the camera to stop sending live data to the computer
    EdsError err = EDS_ERR_OK;
    EdsUInt32 device;
    err = EdsGetPropertyData(m_cam, kEdsPropID_Evf_OutputDevice, 0, sizeof(EdsUInt32), &device);

    if (err) {
        *s_err << "Unable to get live view output device: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        return false;
    }

    device &= ~kEdsEvfOutputDevice_PC;
    err = EdsSetPropertyData(m_cam, kEdsPropID_Evf_OutputDevice, 0, sizeof(EdsUInt32), &device);

    if (err) {
        *s_err << "Unable to turn off live view: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        return false;
    }

    m_liveView->m_state = LiveView::WaitingToStop;

    return true;
}

bool Camera::autoFocus()
{
    EdsUInt32 off = (EdsUInt32) kEdsEvfDepthOfFieldPreview_OFF;
    EdsError err;

    EdsUInt32 currentDepth;
    err = EdsGetPropertyData(m_cam, kEdsPropID_Evf_DepthOfFieldPreview, 0, sizeof(EdsUInt32), &currentDepth);
    if (err) {
        *s_err << "Unable to get depth of field preview: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        return false;
    }
    if (currentDepth != kEdsEvfDepthOfFieldPreview_OFF) {
        // turn OFF depth of field preview
        err = EdsSetPropertyData(m_cam, kEdsPropID_Evf_DepthOfFieldPreview, 0, sizeof(EdsUInt32), &off);

        if (err) {
            *s_err << "Unable to set depth of field preview: " << ErrorMap::errorMsg(err);
            pushErrMsg();
            return false;
        }
    }

    err = EdsSendCommand(m_cam, (EdsUInt32)kEdsCameraCommand_DoEvfAf, (EdsUInt32)kEdsCameraCommand_EvfAf_OFF);
    if (err) {
        *s_err << "Unable to turn live view auto focus off: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        return false;
    }

    err = EdsSendCommand(m_cam, (EdsUInt32)kEdsCameraCommand_DoEvfAf, (EdsUInt32)kEdsCameraCommand_EvfAf_ON);
    if (err) {
        *s_err << "Unable to turn live view auto focus on: " << ErrorMap::errorMsg(err);
        pushErrMsg();
        return false;
    }

    return true;
}

void Camera::terminate()
{
    delete s_err;
    EdsTerminateSDK();
    s_initialized = false;
}

void Camera::pushErrMsg(ErrorLevel level)
{
    if (level >= s_errorLevel) {
        ErrorMessage msg;
        msg.level = level;
        msg.msg = s_err->str();
        s_errMsgQueue.push(msg);
    }

    delete s_err;
    s_err = new stringstream;
}

int Camera::errMsgQueueSize()
{
    return s_errMsgQueue.size();
}

Camera::ErrorMessage Camera::popErrMsg()
{
    if (errMsgQueueSize() == 0) {
        return ErrorMessage();
    } else {
        ErrorMessage value = s_errMsgQueue.front();
        s_errMsgQueue.pop();
        return value;
    }
}

void Camera::setErrorLevel(ErrorLevel level)
{
    s_errorLevel = level;
}
