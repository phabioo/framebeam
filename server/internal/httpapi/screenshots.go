package httpapi

import (
	"context"

	"github.com/phabioo/uzume/server/internal/api"
	"github.com/phabioo/uzume/server/internal/hub"
)

// Screenshots (0.19 package 5). Own user only; run members see the screenshots tagged with their run.

func toAPIScreenshot(sc hub.Screenshot) api.Screenshot {
	out := api.Screenshot{Id: mustUUID(sc.ID), GameId: mustUUID(sc.GameID), UserId: sc.UserID, UserName: sc.UserName,
		CreatedAt: sc.CreatedAt, Width: sc.Width, Height: sc.Height, SizeBytes: sc.SizeBytes}
	if sc.RunID != nil {
		id := mustUUID(*sc.RunID)
		out.RunId = &id
	}
	return out
}

func toAPIScreenshotList(l hub.ScreenshotList) api.ScreenshotList {
	out := api.ScreenshotList{Screenshots: make([]api.Screenshot, 0, len(l.Screenshots)), TotalBytes: l.TotalBytes}
	for _, sc := range l.Screenshots {
		out.Screenshots = append(out.Screenshots, toAPIScreenshot(sc))
	}
	return out
}

func (s *Server) UploadScreenshot(ctx context.Context, req api.UploadScreenshotRequestObject) (api.UploadScreenshotResponseObject, error) {
	run := ""
	if req.Params.RunId != nil {
		run = req.Params.RunId.String()
	}
	sc, err := s.svc.AddScreenshot(ctx, principal(ctx).User.ID, req.GameId.String(), run, req.Body)
	if err != nil {
		return nil, err
	}
	return api.UploadScreenshot201JSONResponse(toAPIScreenshot(sc)), nil
}

func (s *Server) ListScreenshots(ctx context.Context, req api.ListScreenshotsRequestObject) (api.ListScreenshotsResponseObject, error) {
	if _, err := s.svc.GetGameFor(ctx, principal(ctx).User.ID, req.GameId.String()); err != nil {
		return nil, err
	}
	l, err := s.svc.ListScreenshots(ctx, principal(ctx).User.ID, req.GameId.String())
	if err != nil {
		return nil, err
	}
	return api.ListScreenshots200JSONResponse(toAPIScreenshotList(l)), nil
}

func (s *Server) ListRunScreenshots(ctx context.Context, req api.ListRunScreenshotsRequestObject) (api.ListRunScreenshotsResponseObject, error) {
	l, err := s.svc.ListRunScreenshots(ctx, principal(ctx).User.ID, req.RunId.String())
	if err != nil {
		return nil, err
	}
	return api.ListRunScreenshots200JSONResponse(toAPIScreenshotList(l)), nil
}

func (s *Server) DownloadScreenshotContent(ctx context.Context, req api.DownloadScreenshotContentRequestObject) (api.DownloadScreenshotContentResponseObject, error) {
	f, sc, err := s.svc.OpenScreenshot(ctx, principal(ctx).User.ID, req.ScreenshotId.String())
	if err != nil {
		return nil, err
	}
	return api.DownloadScreenshotContent200ImagepngResponse{Body: f, Headers: api.DownloadScreenshotContent200ResponseHeaders{
		ContentLength: ptr(sc.SizeBytes)}}, nil
}

func (s *Server) DeleteScreenshot(ctx context.Context, req api.DeleteScreenshotRequestObject) (api.DeleteScreenshotResponseObject, error) {
	if err := s.svc.DeleteScreenshot(ctx, principal(ctx).User.ID, req.ScreenshotId.String()); err != nil {
		return nil, err
	}
	return api.DeleteScreenshot204Response{}, nil
}
