#!/usr/bin/env python3
"""One stationary block per invocation; fixed overhead camera, no conveyor."""
import argparse
import json
import math
import time
from vision_xy import load, number, pair, request, fresh_frame, observation


def project(camera, pixel, z):
    """Pinhole ray / horizontal block-top plane intersection in base cm.

    Camera u points right (-base Y), v points down (-base X), optical axis
    points vertically down. yaw_deg rotates these axes about base +Z.
    fx/fy/cx/cy refer to the full image coordinates in the detector JSON.
    """
    u, v = pair(pixel)
    x, y, height = [number(n) for n in camera['position_cm']]
    fx, fy = number(camera['fx']), number(camera['fy'])
    if fx <= 0 or fy <= 0 or height <= z:
        raise ValueError('invalid camera focal length or plane height')
    dx = -(v-number(camera['cy']))/fy*(height-z)
    dy = -(u-number(camera['cx']))/fx*(height-z)
    yaw = math.radians(number(camera['yaw_deg']))
    return [x+dx*math.cos(yaw)-dy*math.sin(yaw),
            y+dx*math.sin(yaw)+dy*math.cos(yaw)]


def target(frame, cfg, class_name=None):
    if frame.get('coordinate_frame') != 'camera_pixel':
        raise ValueError('expected camera_pixel coordinates')
    camera = cfg['camera']
    if [frame.get('image_width'), frame.get('image_height')] != camera['image_size'] or frame.get('input_fraction', 1) != camera['input_fraction']:
        raise ValueError('image dimensions/crop differ from camera configuration')
    candidates = [o for o in frame['objects'] if o.get('class_name') in cfg['zones']
                  and (class_name is None or o['class_name'] == class_name)
                  and number(cfg['min_confidence']) <= number(o.get('detection_confidence', 0)) <= 1]
    if len(candidates) != 1:
        raise ValueError('require exactly one matching block; isolate one or specify --class-name')
    obj = candidates[0]
    pixel = pair([obj['cx'], obj['cy']])
    if not all(0 <= pixel[i] < camera['image_size'][i] for i in range(2)):
        raise ValueError('pixel outside image')
    xy = project(camera, pixel, number(cfg['block_top_z_cm']))
    bounds = cfg['workspace_xy_cm']
    if not all(number(bounds[i][0]) <= xy[i] <= number(bounds[i][1]) for i in range(2)):
        raise ValueError('target outside configured workspace')
    return obj, xy


def command(cfg, state, obj, xy, run=False):
    reference = observation(state)
    if state.get('coordinateOriginEnabled'):
        raise ValueError('restore arm base coordinates before fixed-camera sorting')
    if state.get('mode') != 'simulation' and cfg.get('measured') is not True:
        raise ValueError('assumed camera/plane/gripper settings are simulation only')
    recipe = dict(cfg['motion'])
    recipe.update(version=1, confirmed=True, coordinate_frame='arm_current_cm',
                  unknown_class_policy='hold', vision_reference=reference,
                  observation_pulses_us=reference['pulses'], zones=cfg['zones'])
    return dict(action='fixed-sort-run' if run else 'fixed-sort-preview',
                cameraMeasured=cfg.get('measured') is True, confirm=run,
                config=recipe, className=obj['class_name'], xy=xy)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', required=True)
    p.add_argument('--latest', default='/tmp/cubes_latest.json')
    p.add_argument('--socket', default='/tmp/hdmi-ui-arm.sock')
    p.add_argument('--class-name')
    p.add_argument('--run', action='store_true')
    p.add_argument('--offline', action='store_true', help='projection only; no socket or freshness check')
    args = p.parse_args()
    if args.offline and args.run:
        p.error('--offline cannot execute motion')
    cfg = load(args.config)
    if args.offline:
        obj, xy = target(load(args.latest), cfg, args.class_name)
        print(json.dumps(dict(class_name=obj['class_name'], xy_cm=xy, assumed=not cfg['measured'])))
        return
    state = request(args.socket, dict(action='status'))
    observation(state)
    # Require three distinct fresh frames with consistent class and position.
    after = time.time()*1000+300
    previous = None
    for _ in range(3):
        frame = fresh_frame(args.latest, after)
        obj, xy = target(frame, cfg, args.class_name)
        if previous and (previous[0] != obj['class_name'] or math.hypot(xy[0]-previous[1][0], xy[1]-previous[1][1]) > number(cfg['stability_cm'])):
            raise ValueError('block moved or class changed; repeat after it is stationary')
        previous = obj['class_name'], xy
        after = frame['timestamp_ms']+1
    preview = command(cfg, state, obj, xy)
    print(json.dumps(request(args.socket, preview), ensure_ascii=False))
    if not args.run:
        return
    # The server rechecks observation pose and every waypoint before starting.
    if time.time()*1000-frame['timestamp_ms'] > 500:
        raise ValueError('detection expired during planning; retry')
    result = request(args.socket, command(cfg, state, obj, xy, True))
    job = result['sortJobId']
    try:
        deadline = time.monotonic()+180
        while time.monotonic() < deadline:
            state = request(args.socket, dict(action='status'))
            if state['sortJobId'] != job or state['sortJobState'] not in ('running', 'completed'):
                raise ValueError('sorting interrupted or replaced')
            if state['sortJobState'] == 'completed':
                print('Grab/place command sequence completed; physical grasp is not sensed.')
                return
            time.sleep(.1)
        raise ValueError('sorting timed out')
    finally:
        # Stop only our still-running job, never a replacement manual task.
        state = request(args.socket, dict(action='status'))
        if state.get('sortJobId') == job and state.get('sortJobState') == 'running':
            request(args.socket, dict(action='stop'))


if __name__ == '__main__':
    main()
