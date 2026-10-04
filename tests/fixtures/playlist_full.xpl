<?xml version="1.0" encoding="UTF-8"?>
<Playlist xmlns="http://www.dvdforum.org/2005/HDDVDVideo/Playlist"
          majorVersion="1" minorVersion="0" displayName="Original test playlist" type="Advanced">
  <Configuration>
    <StreamingBuffer size="4096"/>
    <Aperture size="1920x1080"/>
    <MainVideoDefaultColor color="108080"/>
    <NetworkTimeout timeout="15000"/>
  </Configuration>
  <MediaAttributeList>
    <VideoAttributeItem index="1" codec="VC-1"/>
    <AudioAttributeItem index="2" codec="DD+" channels="2"/>
    <SubpictureAttributeItem index="3" codec="8bitRLC"/>
  </MediaAttributeList>
  <TitleSet timeBase="60fps" tickBase="60fps" defaultLanguage="en">
    <FirstPlayTitle titleDuration="00:00:02:00">
      <PrimaryAudioVideoClip src="file:///dvddisc/HVDVD_TS/intro.MAP"
          titleTimeBegin="00:00:00:00" titleTimeEnd="00:00:02:00" seamless="false">
        <Video track="1" mediaAttr="1"/>
      </PrimaryAudioVideoClip>
    </FirstPlayTitle>
    <Title id="feature" titleNumber="1" type="Advanced" displayName="Feature"
           selectable="true" titleDuration="00:02:00:00" onEnd="menu">
      <PrimaryAudioVideoClip src="file:///dvddisc/HVDVD_TS/part1.MAP"
          titleTimeBegin="00:00:00:00" titleTimeEnd="00:01:00:00"
          clipTimeBegin="00:00:03:00" seamless="false" dataSource="Disc">
        <Video track="1" mediaAttr="1"/>
        <Audio track="2" streamNumber="1" mediaAttr="2"/>
      </PrimaryAudioVideoClip>
      <PrimaryAudioVideoClip src="file:///dvddisc/HVDVD_TS/part2.MAP"
          titleTimeBegin="00:01:00:00" titleTimeEnd="00:02:00:00" seamless="true"/>
      <SecondaryAudioVideoClip src="file:///dvddisc/HVDVD_TS/commentary.MAP"
          titleTimeBegin="00:00:30:00" titleTimeEnd="00:01:00:00"/>
      <ApplicationSegment src="file:///dvddisc/ADV_OBJ/app.aca/segment.xmf" autorun="true"
          loadingBegin="00:00:04:30"
          zOrder="2" titleTimeBegin="00:00:05:00" titleTimeEnd="00:01:55:00"/>
      <ChapterList>
        <Chapter id="chapter-1" displayName="One" titleTimeBegin="00:00:00:00"/>
        <Chapter id="chapter-2" displayName="Two" titleTimeBegin="00:01:00:00"/>
      </ChapterList>
      <ScheduledControlList>
        <Event id="reminder" titleTime="00:01:30:00"/>
        <PauseAt titleTime="00:01:40:00"/>
      </ScheduledControlList>
      <TrackNavigationList>
        <VideoTrack track="1" selectable="true"/>
        <AudioTrack track="2" selectable="true" langcode="en:00"/>
        <SubpictureTrack track="3" selectable="false" langcode="en:01"/>
      </TrackNavigationList>
    </Title>
    <Title id="menu" titleNumber="2" titleDuration="00:00:10:00" onEnd="menu"/>
    <PlaylistApplication language="en" src="file:///dvddisc/ADV_OBJ/global.aca/app.xmf"
                         description="Original global application">
      <PlaylistApplicationResource size="1234" src="file:///dvddisc/ADV_OBJ/global.aca"
                                   multiplexed="1"/>
    </PlaylistApplication>
  </TitleSet>
</Playlist>
