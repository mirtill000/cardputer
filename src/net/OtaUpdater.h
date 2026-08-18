#pragma once

#include <Arduino.h>

// One-shot, synchronous, HTTP-based firmware update: downloads a raw
// firmware .bin from `url` and flashes it into the inactive OTA slot.
// "Synchronous" is deliberate here, not an oversight - see OtaScreen.h
// for why blocking the UI task for the download's duration is an
// accepted tradeoff for a rare, user-initiated, "device is unavailable
// while this runs" operation, instead of building out a background-task
// + progress-queue path for it.
//
// On success this does not return - it reboots the device itself via
// ESP.restart() once Update.end() has validated the new image is a
// complete, well-formed app image. Nothing is left half-flashed on
// failure: the OTA slot being written to is never marked bootable until
// that validation passes, so a failed/interrupted update just leaves
// the currently-running firmware as the one that boots next time too.
namespace OtaUpdater {

// Returns false and fills errorOut with a short reason on any failure
// (bad URL, HTTP error, image too big for the inactive slot, a
// truncated download, a corrupt image). Only ever returns on failure -
// success reboots the device from inside this call.
bool run(const String& url, String& errorOut);

}  // namespace OtaUpdater
